/*
 *	Filters: utility functions
 *
 *	Copyright 1998 Pavel Machek <pavel@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 *
 * 	Notice that pair is stored as integer: first << 16 | second
 *
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/signal.h>
#include <setjmp.h>

#include "nest/bird.h"
#include "lib/lists.h"
#include "lib/resource.h"
#include "lib/socket.h"
#include "lib/string.h"
#include "nest/route.h"
#include "nest/protocol.h"
#include "nest/iface.h"
#include "conf/conf.h"
#include "filter/filter.h"

#define P(a,b) ((a<<8) | b)

struct f_inst *startup_func = NULL;

#define CMP_ERROR 999

/* Compare two values, returns -1, 0, 1 compared, ERROR 999 */
int
val_compare(struct f_val v1, struct f_val v2)
{
  if ((v1.type == T_VOID) && (v2.type == T_VOID))
    return 0;
  if (v1.type == T_VOID)	/* Hack for else */
    return -1;
  if (v2.type == T_VOID)
    return 1;

  if (v1.type != v2.type)
    return CMP_ERROR;
  switch (v1.type) {
  case T_ENUM:
  case T_INT: 
  case T_PAIR:
    if (v1.val.i == v2.val.i) return 0;
    if (v1.val.i < v2.val.i) return -1;
    return 1;
  case T_IP:
  case T_PREFIX:
    return ipa_compare(v1.val.px.ip, v2.val.px.ip);
  default: { printf( "Error comparing\n" ); return CMP_ERROR; }
  }
}

int 
val_simple_in_range(struct f_val v1, struct f_val v2)
{
  if ((v1.type == T_IP) && (v2.type == T_PREFIX))
    return !(ipa_compare(ipa_and(v2.val.px.ip, ipa_mkmask(v2.val.px.len)), ipa_and(v1.val.px.ip, ipa_mkmask(v2.val.px.len))));

  if ((v1.type == T_PREFIX) && (v2.type == T_PREFIX)) {
    ip_addr mask;
    if (v1.val.px.len & (LEN_PLUS | LEN_MINUS | LEN_RANGE))
      return CMP_ERROR;
    mask = ipa_mkmask( v2.val.px.len & LEN_MASK );
    if (ipa_compare(ipa_and(v2.val.px.ip, mask), ipa_and(v1.val.px.ip, mask)))
      return 0;

    if ((v2.val.px.len & LEN_MINUS) && (v1.val.px.len <= (v2.val.px.len & LEN_MASK)))
      return 0;
    if ((v2.val.px.len & LEN_PLUS) && (v1.val.px.len < (v2.val.px.len & LEN_MASK)))
      return 0;
    if ((v2.val.px.len & LEN_RANGE) && ((v1.val.px.len < (0xff & (v2.val.px.len >> 16)))
					|| (v1.val.px.len > (0xff & (v2.val.px.len >> 8)))))
      return 0;
    return 1;    
  }
  return CMP_ERROR;
}

int
val_in_range(struct f_val v1, struct f_val v2)
{
  int res;

  res = val_simple_in_range(v1, v2);

  if (res != CMP_ERROR)
    return res;

  if (((v1.type == T_INT) || ((v1.type == T_IP) || (v1.type == T_PREFIX)) && (v2.type == T_SET))) {
    struct f_tree *n;
    n = find_tree(v2.val.t, v1);
    if (!n)
      return 0;
    return !! (val_simple_in_range(v1, n->from));	/* We turn CMP_ERROR into compared ok, and that's fine */
  }
  return CMP_ERROR;
}

static void
tree_print(struct f_tree *t)
{
  if (!t) {
    printf( "() " );
    return;
  }
  printf( "[ " );
  tree_print( t->left );
  printf( ", " ); val_print( t->from ); printf( ".." ); val_print( t->to ); printf( ", " );
  tree_print( t->right );
  printf( "] " );
}

void
val_print(struct f_val v)
{
  char buf[2048];
#define PRINTF(a...) bsnprintf( buf, 2040, a )
  buf[0] = 0;
  switch (v.type) {
  case T_VOID: PRINTF( "(void)" ); break;
  case T_BOOL: PRINTF( v.val.i ? "TRUE" : "FALSE" ); break;
  case T_INT: PRINTF( "%d ", v.val.i ); break;
  case T_STRING: PRINTF( "%s", v.val.s ); break;
  case T_IP: PRINTF( "%I", v.val.px.ip ); break;
  case T_PREFIX: PRINTF( "%I/%d", v.val.px.ip, v.val.px.len ); break;
  case T_PAIR: PRINTF( "(%d,%d)", v.val.i >> 16, v.val.i & 0xffff ); break;
  case T_SET: tree_print( v.val.t ); PRINTF( "\n" ); break;
  case T_ENUM: PRINTF( "(enum %x)%d", v.type, v.val.i ); break;
  default: PRINTF( "[unknown type %x]", v.type );
  }
  printf( buf );
}

static struct rte **f_rte, *f_rte_old;
static struct linpool *f_pool;
static struct ea_list **f_tmp_attrs;

#define runtime(x) do { \
    log( L_ERR x ); \
    res.type = T_RETURN; \
    res.val.i = F_ERROR; \
    return res; \
  } while(0)

#define ARG(x,y) \
	x = interpret(what->y); \
	if (x.type & T_RETURN) \
		return x;

#define ONEARG ARG(v1, a1.p)
#define TWOARGS ARG(v1, a1.p) \
		ARG(v2, a2.p)
#define TWOARGS_C TWOARGS \
                  if (v1.type != v2.type) \
		    runtime( "Can not operate with values of incompatible types" );

static struct f_val
interpret(struct f_inst *what)
{
  struct symbol *sym;
  struct f_val v1, v2, res;
  int i,j,k;

  res.type = T_VOID;
  if (!what)
    return res;

  switch(what->code) {
  case ',':
    TWOARGS;
    break;

/* Binary operators */
  case '+':
    TWOARGS_C;
    switch (res.type = v1.type) {
    case T_VOID: runtime( "Can not operate with values of type void" );
    case T_INT: res.val.i = v1.val.i + v2.val.i; break;
    default: runtime( "Usage of unknown type" );
    }
    break;
  case '/':
    TWOARGS_C;
    switch (res.type = v1.type) {
    case T_VOID: runtime( "Can not operate with values of type void" );
    case T_INT: res.val.i = v1.val.i / v2.val.i; break;
    case T_IP: if (v2.type != T_INT)
                 runtime( "Operator / is <ip>/<int>" );
               break;
    default: runtime( "Usage of unknown type" );
    }
    break;

/* Relational operators */

#define COMPARE(x) \
    TWOARGS_C; \
    res.type = T_BOOL; \
    i = val_compare(v1, v2); \
    if (i==CMP_ERROR) \
      runtime( "Error in comparation" ); \
    res.val.i = (x); \
    break;

  case P('!','='): COMPARE(i!=0);
  case P('=','='): COMPARE(i==0);
  case '<': COMPARE(i==-1);
  case P('<','='): COMPARE(i!=1);

  case '!':
    ONEARG;
    if (v1.type != T_BOOL)
      runtime( "not applied to non-boolean" );
    res = v1;
    res.val.i = !res.val.i;
    break;

  case '~':
    TWOARGS;
    res.type = T_BOOL;
    res.val.i = val_in_range(v1, v2);
    if (res.val.i == CMP_ERROR)
      runtime( "~ applied on unknown type pair" );
    break;
  case P('d','e'):
    ONEARG;
    res.type = T_BOOL;
    res.val.i = (v1.type != T_VOID);
    break;

  /* Set to indirect value, a1 = variable, a2 = value */
  case 's':
    ARG(v2, a2.p);
    sym = what->a1.p;
    switch (res.type = v2.type) {
    case T_VOID: runtime( "Can not assign void values" );
    case T_ENUM:
    case T_INT: 
    case T_IP: 
    case T_PREFIX: 
    case T_PAIR: 
      if (sym->class != (SYM_VARIABLE | v2.type))
	runtime( "Variable of bad type" );
      * (struct f_val *) sym->aux2 = v2; 
      break;
    default:
      bug( "Set to invalid type\n" );
    }
    break;

  case 'c':	/* integer (or simple type) constant */
    res.type = what->aux;
    res.val.i = what->a2.i;
    break;
  case 'C':
    res = * ((struct f_val *) what->a1.p);
    break;
  case 'p':
    ONEARG;
    val_print(v1);
    break;
  case '?':	/* ? has really strange error value, so we can implement if ... else nicely :-) */
    ONEARG;
    if (v1.type != T_BOOL)
      runtime( "If requires bool expression" );
    if (v1.val.i) {
      ARG(res,a2.p);
      res.val.i = 0;
    } else res.val.i = 1;
    res.type = T_BOOL;
    break;
  case '0':
    printf( "No operation\n" );
    break;
  case P('p',','):
    ONEARG;
    if (what->a2.i != F_NONL)
      printf( "\n" );

    switch (what->a2.i) {
    case F_QUITBIRD:
      die( "Filter asked me to die" );
    case F_ACCEPT:
      /* Should take care about turning ACCEPT into MODIFY */
    case F_ERROR:
    case F_REJECT:	/* FIXME (noncritical) Should print complete route along with reason to reject route */
      res.type = T_RETURN;
      res.val.i = what->a2.i;
      return res;	/* We have to return now, no more processing. */
    case F_NONL:
    case F_NOP:
      break;
    default:
      bug( "unknown return type: can not happen");
    }
    break;
  case 'a':	/* rta access */
    {
      struct rta *rta = (*f_rte)->attrs;
      res.type = what->aux;
      switch(res.type) {
      case T_IP:
	res.val.px.ip = * (ip_addr *) ((char *) rta + what->a2.i);
	break;
      case T_ENUM:
	res.val.i = * ((char *) rta + what->a2.i);
	break;
      case T_PREFIX:	/* Warning: this works only for prefix of network */
	{
	  res.val.px.ip = (*f_rte)->net->n.prefix;
	  res.val.px.len = (*f_rte)->net->n.pxlen;
	  break;
	}
      default:
	bug( "Invalid type for rta access (%x)\n" );
      }
    }
    break;
  case P('e','a'):	/* Access to extended attributes */
    {
      eattr *e = ea_find( (*f_rte)->attrs->eattrs, what->a2.i );
      if (!e) 
	e = ea_find( (*f_tmp_attrs), what->a2.i );
      if (!e) {
	res.type = T_VOID;
	break;
      }
      res.type = what->aux;
      switch (what->a1.i) {
      case T_INT:
	res.val.i = e->u.data;
	break;
      }
    }
    break;
  case P('e','S'):
    ONEARG;
    if (v1.type != what->aux)
      runtime("Wrong type when setting dynamic attribute\n");

    {
      struct ea_list *l = lp_alloc(f_pool, sizeof(struct ea_list) + sizeof(eattr));

      l->next = NULL;
      l->flags = EALF_SORTED;
      l->count = 1;
      l->attrs[0].id = what->a2.i;
      l->attrs[0].flags = 0;
      l->attrs[0].type = what->aux;
      switch (what->aux & EAF_TYPE_MASK) {
      case EAF_TYPE_INT:
	if (v1.type != T_INT)
	  runtime( "Setting int attribute to non-int value" );
	l->attrs[0].u.data = v1.val.i;
	break;
      case EAF_TYPE_UNDEF:
	if (v1.type != T_VOID)
	  runtime( "Setting void attribute to non-void value" );
	l->attrs[0].u.data = 0;
	break;
      }

      if (!(what->aux & EAF_TEMP)) {
	*f_rte = rte_do_cow(*f_rte);
	l->next = (*f_rte)->attrs->eattrs;
	(*f_rte)->attrs->eattrs = l;
      } else {
	l->next = (*f_tmp_attrs);
	(*f_tmp_attrs) = l;
      }
    }
    break;

  case P('c','p'):	/* Convert prefix to ... */
    ONEARG;
    if (v1.type != T_PREFIX)
      runtime( "Can not convert non-prefix this way" );
    res.type = what->aux;
    switch(res.type) {
    case T_INT:	res.val.i = v1.val.px.len; break;
    case T_IP: res.val.px.ip = v1.val.px.ip; break;
    default: bug( "Unknown prefix to conversion\n" );
    }
    break;
  case 'r':
    ONEARG;
    res = v1;
    res.type |= T_RETURN;
    break;
  case P('c','a'): /* CALL: this is special: if T_RETURN and returning some value, mask it out  */
    ONEARG;
    res = interpret(what->a2.p);
    if (res.type == T_RETURN)
      return res;
    res.type &= ~T_RETURN;    
    break;
  case P('S','W'):
    ONEARG;
    {
      struct f_tree *t = find_tree(what->a2.p, v1);
      if (!t) {
	v1.type = T_VOID;
	t = find_tree(what->a2.p, v1);
	if (!t) {
	  printf( "No else statement?\n ");
	  break;
	}
      }	
      if (!t->data)
	die( "Impossible: no code associated!\n" );
      return interpret(t->data);
    }
    break;
  case P('i','M'): /* IP.MASK(val) */
    TWOARGS;
    if (v2.type != T_INT)
      runtime( "Can not use this type for mask.");
    if (v1.type != T_IP)
      runtime( "You can mask only IP addresses." );
    {
      ip_addr mask = ipa_mkmask(v2.val.i);
      res.type = T_IP;
      res.val.px.ip = ipa_and(mask, v1.val.px.ip);
    }
    break;
  default:
    bug( "Unknown instruction %d (%c)", what->code, what->code & 0xff);
  }
  if (what->next)
    return interpret(what->next);
  return res;
}

#undef ARG
#define ARG(x,y) \
	if (!i_same(f1->y, f2->y)) \
		return 0;

#define ONEARG ARG(v1, a1.p)
#define TWOARGS ARG(v1, a1.p) \
		ARG(v2, a2.p)

#define A2_SAME if (f1->a2.i != f2->a2.i) return 0;

int
i_same(struct f_inst *f1, struct f_inst *f2)
{
  if ((!!f1) != (!!f2))
    return 0;
  if (!f1)
    return 1;
  if (f1->aux != f2->aux)
    return 0;
  if (f1->code != f2->code)
    return 0;
  if (f1 == f2)		/* It looks strange, but it is possible with call rewriting trickery */
    return 1;

  switch(f1->code) {
  case ',': /* fall through */
  case '+':
  case '/':
  case P('!','='):
  case P('=','='):
  case '<':
  case P('<','='): TWOARGS; break;

  case '!': ONEARG; break;
  case '~': TWOARGS; break;
  case P('d','e'): ONEARG; break;

  case 's':
    ARG(v2, a2.p);
    {
      struct symbol *s1, *s2;
      s1 = f1->a1.p;
      s2 = f2->a1.p;
      if (strcmp(s1->name, s2->name))
	return 0;
      if (s1->class != s2->class)
	return 0;
    }
    break;

  case 'c': A2_SAME; break;
  case 'C': 
    if (val_compare(* (struct f_val *) f1->a1.p, * (struct f_val *) f2->a2.p))
      return 0;
    break;
  case 'p': ONEARG; break;
  case '?': TWOARGS; break;
  case '0': break;
  case P('p',','): ONEARG; A2_SAME; break;
  case 'a': A2_SAME; break;
  case P('e','a'): A2_SAME; break;
  case P('e','S'): ONEARG; A2_SAME; break;

  case 'r': ONEARG; break;
  case P('c','p'): ONEARG; break;
  case P('c','a'): /* Call rewriting trickery to avoid exponential behaviour */
             ONEARG; 
	     if (!i_same(f1->a2.p, f2->a2.p))
	       return 0; 
	     f2->a2.p = f1->a2.p;
	     break;
  case P('S','W'): ONEARG; if (!same_tree(f1->a2.p, f2->a2.p)) return 0; break;
  case P('i','M'): TWOARGS; break;
  default:
    bug( "Unknown instruction %d in same (%c)", f1->code, f1->code & 0xff);
  }
  return i_same(f1->next, f2->next);
}

int
f_run(struct filter *filter, struct rte **rte, struct ea_list **tmp_attrs, struct linpool *tmp_pool)
{
  struct f_inst *inst;
  struct f_val res;
  debug( "Running filter `%s'...", filter->name );

  f_tmp_attrs = tmp_attrs;
  f_rte = rte;
  f_rte_old = *rte;
  f_pool = tmp_pool;
  inst = filter->root;
  res = interpret(inst);
  if (res.type != T_RETURN)
    return F_ERROR;
  debug( "done (%d)\n", res.val.i );
  return res.val.i;
}

void
filters_postconfig(void)
{
  struct f_val res;
  if (startup_func) {
    printf( "Launching startup function...\n" );
    res = interpret(startup_func);
    if (res.type == F_ERROR)
      die( "Startup function resulted in error." );
    printf( "done\n" );
  }
} 

int
filter_same(struct filter *new, struct filter *old)
{
  return i_same(new->root, old->root);
}
