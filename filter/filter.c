/*
 *	Filters: utility functions
 *
 *	Copyright 1998 Pavel Machek <pavel@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 *
 *	FIXME: local namespace for functions
 *
 * 	Notice that pair is stored as integer: first << 16 | second
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

struct f_inst *startup_func = NULL;

#define runtime(x) do { \
    log( L_ERR x ); \
    res.type = T_RETURN; \
    res.val.i = F_ERROR; \
    return res; \
  } while(0)

#define ARG(x,y) \
	x = interpret(what->y); \
	if (x.type == T_RETURN) \
		return x;

#define ONEARG ARG(v1, a1.p)
#define TWOARGS ARG(v1, a1.p) \
		ARG(v2, a2.p)
#define TWOARGS_C TWOARGS \
                  if (v1.type != v2.type) \
		    runtime( "Can not operate with values of incompatible types" );

#define CMP_ERROR 999

/* Compare two values, returns -1, 0, 1 compared, ERROR 999 */
int
val_compare(struct f_val v1, struct f_val v2)
{
  if (v1.type != v2.type)
    return CMP_ERROR;
  switch (v1.type) {
  case T_INT: 
    if (v1.val.i == v2.val.i) return 0;
    if (v1.val.i < v2.val.i) return -1;
    return 1;
  case T_IP:
    return ipa_compare(v1.val.ip, v2.val.ip);
  default: return CMP_ERROR;
  }
}

int
val_in_range(struct f_val v1, struct f_val v2)
{
  if (((v1.type == T_INT) || (v1.type == T_IP)) && (v2.type == T_SET))
    return !! find_tree(v2.val.t, v1);
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
  case T_IP: PRINTF( "%I", v.val.ip ); break;
  case T_PREFIX: PRINTF( "%I/%d", v.val.px.ip, v.val.px.len ); break;
  case T_PAIR: PRINTF( "(%d,%d)", v.val.i >> 16, v.val.i & 0xffff ); break;
  case T_SET: tree_print( v.val.t ); PRINTF( "\n" ); break;
  default: PRINTF( "[unknown type %x]", v.type );
  }
  printf( buf );
}

static struct rte **f_rte;

static struct f_val interpret(struct f_inst *what);

static struct f_val
interpret_switch(struct f_inst *what, struct f_val control)
{
  struct f_val this, res;
  int i;
  res.type = T_VOID;

  if (!what)
    return res;

  switch(what->code) {
  case 'el':
    return interpret(what->a2.p);

  case 'of':
    this = interpret(what->a1.p);
    i = val_compare(control, this);
    if (!i)
      return interpret(what->a2.p);
    if (i==CMP_ERROR) {
      i = val_in_range(control, this);
      if (i==1)
	return interpret(what->a2.p);
      if (i==CMP_ERROR)
	runtime( "incompatible types in case" );
    }
    break;
    
  default:
    bug( "This can not happen (%x)\n", what->code );
  }
  return interpret_switch(what->next, control);
}

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

  case '!=': COMPARE(i!=0);
  case '==': COMPARE(i==0);
  case '<': COMPARE(i==-1);
  case '<=': COMPARE(i!=1);

    /* FIXME: Should be able to work with prefixes of limited sizes */
  case '~':
    TWOARGS;
    res.type = T_BOOL;
    res.val.i = val_in_range(v1, v2);
    if (res.val.i == CMP_ERROR)
      runtime( "~ applied on unknown type pair" );
    break;

  /* Set to consant, a1 = type, a2 = value */
  case 's':
    ARG(v2, a2.p);
    sym = what->a1.p;
    switch (res.type = v2.type) {
    case T_VOID: runtime( "Can not assign void values" );
    case T_INT: 
      if (sym->class != (SYM_VARIABLE | T_INT))
	runtime( "Variable of bad type" );
      sym->aux = v2.val.i; 
      break;
    }
    break;

  case 'c':
    res.type = what->a1.i;
    res.val.i = (int) what->a2.p;
    break;
  case 'C':
    res = * ((struct f_val *) what->a1.p);
    break;
  case 'i':
    res.type = what->a1.i;
    res.val.i = * ((int *) what->a2.p);
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
  case 'p,':
    ONEARG;
    printf( "\n" );

    switch (what->a2.i) {
    case F_QUITBIRD:
      die( "Filter asked me to die" );
    case F_ACCEPT:
      /* Should take care about turning ACCEPT into MODIFY */
    case F_ERROR:
    case F_REJECT:
      res.type = T_RETURN;
      res.val.i = what->a1.i;
      break;
    case F_NOP:
      break;
    default:
      bug( "unknown return type: can not happen");
    }
    break;
  case 'a':	/* rta access */
    {
      struct rta *rta = (*f_rte)->attrs;
      res.type = what->a1.i;
      switch(res.type) {
      case T_IP:
	res.val.ip = * (ip_addr *) ((char *) rta + what->a2.i);
	break;
      case T_PREFIX:	/* Warning: this works only for prefix of network */
	{
	  res.val.px.ip = (*f_rte)->net->n.prefix;
	  res.val.px.len = (*f_rte)->net->n.pxlen;
	  break;
	}
      default:
	bug( "Invalid type for rta access" );
      }
    }
    break;
  case 'cp':	/* Convert prefix to ... */
    ONEARG;
    if (v1.type != T_PREFIX)
      runtime( "Can not convert non-prefix this way" );
    res.type = what->a2.i;
    switch(res.type) {
    case T_INT:	res.val.i = v1.val.px.len; break;
    case T_IP: res.val.ip = v1.val.px.ip; break;
    default: bug( "Unknown prefix to conversion\n" );
    }
    break;
  case 'ca': /* CALL */
    ONEARG;
    res = interpret(what->a2.p);
    break;
  case 'sw': /* SWITCH alias CASE */
    ONEARG;
    interpret_switch(what->a2.p, v1);
    break;
  default:
    bug( "Unknown instruction %d (%c)", what->code, what->code & 0xff);
  }
  if (what->next)
    return interpret(what->next);
  return res;
}

int
f_run(struct filter *filter, struct rte **rte, struct ea_list **tmp_attrs, struct linpool *tmp_pool)
{
  struct f_inst *inst;
  struct f_val res;
  debug( "Running filter `%s'...", filter->name );

  f_rte = rte;
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
