/*
 *	BIRD - filters
 *
 *	Copyright 1998,1999 Pavel Machek
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 *
	FIXME (nonurgent): define keyword
	FIXME (for BGP): whole system of paths, path ~ string, path.prepend(), path.originate
	FIXME: create community lists
	FIXME: IP addresses in ipv6
 */

CF_HDR

#include "nest/bird.h"
#include "lib/resource.h"
#include "lib/socket.h"
#include "lib/timer.h"
#include "nest/protocol.h"
#include "nest/iface.h"
#include "nest/route.h"
#include <string.h>

#define P(a,b) ((a<<8) | b)

CF_DECLS

CF_KEYWORDS(FUNCTION, PRINT, PRINTN, CONST, UNSET, RETURN,
	ACCEPT, REJECT, ERROR, QUITBIRD,
	INT, BOOL, IP, PREFIX, PAIR, SET, STRING,
	IF, THEN, ELSE, CASE,
	TRUE, FALSE,
	FROM, GW, NET, MASK, SOURCE,
	LEN,
	DEFINED,
	FILTER, WHERE)

%nonassoc THEN
%nonassoc ELSE

%type <x> term block cmds cmd function_body constant print_one print_list var_list var_listn dynamic_attr function_call
%type <f> filter filter_body where_filter
%type <i> type break_command pair
%type <e> set_item set_items switch_body
%type <v> set_atom prefix prefix_s ipa
%type <s> decls declsn one_decl function_params 

CF_GRAMMAR

CF_ADDTO(conf, filter_def)
filter_def:
   FILTER SYM { cf_push_scope( $2 ); } filter_body {
     cf_define_symbol($2, SYM_FILTER, $4);
     $4->name = $2->name;
     DBG( "We have new filter defined (%s)\n", $2->name );
     cf_pop_scope();
   }
 ;

type:
   INT { $$ = T_INT; }
 | BOOL { $$ = T_BOOL; }
 | IP { $$ = T_IP; }
 | PREFIX { $$ = T_PREFIX; }
 | PAIR { $$ = T_PAIR; }
 | STRING { $$ = T_STRING; }
 | type SET { 
	switch ($1) {
	  default:
		cf_error( "You can not create sets of this type\n" );
	  case T_INT: case T_IP: case T_PREFIX: case T_PAIR:
	}
	$$ = $1 | T_SET;
	}
 ;

one_decl:
   type SYM {
     cf_define_symbol($2, SYM_VARIABLE | $1, NULL);
     DBG( "New variable %s type %x\n", $2->name, $1 );
     $2->aux = 0;
     {
       struct f_val * val; 
       val = cfg_alloc(sizeof(struct f_val)); 
       val->type = $1; 
       $2->aux2 = val;
     }
     $$=$2;
   }
 ;

/* Decls with ';' at the end */
decls: /* EMPTY */ { $$ = NULL; }
 | one_decl ';' decls {
     $$ = $1;
     $$->aux = (int) $3;
   }
 ;

/* Declarations that have no ';' at the end. */
declsn: one_decl { $$ = $1; }
 | declsn ';' one_decl {
     $$ = $1;
     $$->aux = (int) $3;
   }
 ;

filter_body:
   function_body {
     struct filter *f = cfg_alloc(sizeof(struct filter));
     f->name = NULL;
     f->root = $1;
     $$ = f;
   }
 ;

filter:
   SYM {
     if ($1->class != SYM_FILTER) cf_error("No such filter");
     $$ = $1->def;
   }
 | filter_body
 ;

where_filter:
   WHERE term {
     /* Construct 'IF term THEN ACCEPT; REJECT;' */
     struct filter *f = cfg_alloc(sizeof(struct filter));
     struct f_inst *i, *acc, *rej;
     acc = f_new_inst();		/* ACCEPT */
     acc->code = P('p',',');
     acc->a1.p = NULL;
     acc->a2.i = F_ACCEPT;
     rej = f_new_inst();		/* REJECT */
     rej->code = P('p',',');
     rej->a1.p = NULL;
     rej->a2.i = F_REJECT;
     i = f_new_inst();			/* IF */
     i->code = '?';
     i->a1.p = $2;
     i->a2.p = acc;
     i->next = rej;
     f->name = NULL;
     f->root = i;
     $$ = f;
  }
 ;

function_params:
   '(' declsn ')' { DBG( "Have function parameters\n" ); $$=$2; }
 | '(' ')' { $$=NULL; }
 ;

function_body:
   decls '{' cmds '}' {
     $$ = $3;
   }
 ;

CF_ADDTO(conf, function_def)
function_def:
   FUNCTION SYM { DBG( "Begining of function %s\n", $2->name ); cf_push_scope($2); } function_params function_body {
     extern struct f_inst *startup_func;
     cf_define_symbol($2, SYM_FUNCTION, $5);
     if (!strcasecmp($2->name, "startup"))
	startup_func = $5;
     $2->aux = (int) $4;
     $2->aux2 = $5;
     DBG("Hmm, we've got one function here - %s\n", $2->name); 
     cf_pop_scope();
   }
 ;

/* Programs */

cmds: /* EMPTY */ { $$ = NULL; }
 | cmd cmds {
     if ($1) {
       if ($1->next)
	 bug("Command has next already set\n");
       $1->next = $2;
       $$ = $1;
     } else $$ = $2;
   }
 ;

block:
   cmd {
     $$=$1;
   }
 | '{' cmds '}' {
     $$=$2;
   }
 ;

/*
 * Simple types, their bison value is int
 */
pair:
   '(' NUM ',' NUM ')' { $$ = $2 << 16 | $4; }
 ;

/*
 * Complex types, their bison value is struct f_val
 */
prefix_s:
   IPA '/' NUM { $$.type = T_PREFIX; $$.val.px.ip = $1; $$.val.px.len = $3; if (ipa_nonzero(ipa_and($$.val.px.ip, ipa_not(ipa_mkmask($$.val.px.len))))) cf_error( "%I/%d is not really prefix\n", $$.val.px.ip, $$.val.px.len ); }
 ;

prefix:
   prefix_s { $$ = $1; }
 | prefix_s '+' { $$ = $1; $$.val.px.len |= LEN_PLUS; }
 | prefix_s '-' { $$ = $1; $$.val.px.len |= LEN_MINUS; }
 | prefix_s '{' NUM ',' NUM '}' { $$ = $1; $$.val.px.len |= LEN_RANGE | ($3 << 16) | ($5 << 8); }
 ;

ipa:
   IPA  { $$.type = T_IP; $$.val.px.ip = $1; }
 ;

set_atom:
   NUM  { $$.type = T_INT; $$.val.i = $1; }
 | pair { $$.type = T_PAIR; $$.val.i = $1; }
 | ipa  { $$ = $1; }
 | prefix { $$ = $1; }
 ; 

set_item:
   set_atom { $$ = f_new_tree(); $$->from = $$->to = $1 }
 | set_atom '.' '.' set_atom { $$ = f_new_tree(); $$->from = $1; $$->to = $4; }
 ;

set_items:
   set_item { $$ = $1; }
 | set_items ',' set_item { $$ = $3; $$->left = $1; }
 ;

switch_body: /* EMPTY */ { $$ = NULL; }
 | set_item ':' cmds switch_body {
     $$ = $1;
     $$->data = $3;
     $$->left = $4;
   }
 | ELSE ':' cmds {
     $$ = f_new_tree(); 
     $$->from.type = T_VOID; 
     $$->to.type = T_VOID;
     $$->data = $3;
   }
 ;

constant:
   CONST '(' expr ')' { $$ = f_new_inst(); $$->code = 'c'; $$->aux = T_INT; $$->a2.i = $3; }
 | NUM    { $$ = f_new_inst(); $$->code = 'c'; $$->aux = T_INT;  $$->a2.i = $1; }
 | TRUE   { $$ = f_new_inst(); $$->code = 'c'; $$->aux = T_BOOL; $$->a2.i = 1;  }
 | FALSE  { $$ = f_new_inst(); $$->code = 'c'; $$->aux = T_BOOL; $$->a2.i = 0;  }
 | TEXT   { $$ = f_new_inst(); $$->code = 'c'; $$->aux = T_STRING; $$->a2.p = $1; }
 | pair   { $$ = f_new_inst(); $$->code = 'c'; $$->aux = T_PAIR;  $$->a2.i = $1; }
 | ipa	   { NEW_F_VAL; $$ = f_new_inst(); $$->code = 'C'; $$->a1.p = val; *val = $1; }
 | prefix_s {NEW_F_VAL; $$ = f_new_inst(); $$->code = 'C'; $$->a1.p = val; *val = $1; }
 | '[' set_items ']' { DBG( "We've got a set here..." ); $$ = f_new_inst(); $$->code = 'c'; $$->aux = T_SET; $$->a2.p = build_tree($2); DBG( "ook\n" ); }
 | ENUM	  { $$ = f_new_inst(); $$->code = 'c'; $$->aux = $1 >> 16; $$->a2.i = $1 & 0xffff; }
 ;

/*
 *  Maybe there are no dynamic attributes defined by protocols.
 *  For such cases, we force the dynamic_attr list to contain
 *  at least an invalid token, so it's syntantically correct.
 */
CF_ADDTO(dynamic_attr, INVALID_TOKEN { $$ = NULL; })

rtadot: /* EMPTY, we are not permitted RTA. prefix */
 ;

function_call:
   SYM '(' var_list ')' {
     struct symbol *sym;
     struct f_inst *inst = $3;
     if ($1->class != SYM_FUNCTION)
       cf_error("You can not call something which is not function. Really.");
     DBG("You are calling function %s\n", $1->name);
     $$ = f_new_inst();
     $$->code = P('c','a');
     $$->a1.p = inst;
     $$->a2.p = $1->aux2;
     sym = (void *) $1->aux;
     while (sym || inst) {
       if (!sym || !inst)
	 cf_error("wrong number of arguments for function %s.", $1->name);
       DBG( "You should pass parameter called %s\n", sym->name);
       inst->a1.p = sym;
       sym = (void *) sym->aux;
       inst = inst->next;
     }
   }
 ;

term:
   '(' term ')'      { $$ = $2; }
 | term '+' term     { $$ = f_new_inst(); $$->code = '+';        $$->a1.p = $1; $$->a2.p = $3; }
 | term '=' term     { $$ = f_new_inst(); $$->code = P('=','='); $$->a1.p = $1; $$->a2.p = $3; }
 | term NEQ term { $$ = f_new_inst(); $$->code = P('!','=');     $$->a1.p = $1; $$->a2.p = $3; }
 | term '<' term     { $$ = f_new_inst(); $$->code = '<';        $$->a1.p = $1; $$->a2.p = $3; }
 | term LEQ term { $$ = f_new_inst(); $$->code = P('<','=');     $$->a1.p = $1; $$->a2.p = $3; }
 | term '>' term     { $$ = f_new_inst(); $$->code = '<';        $$->a1.p = $3; $$->a2.p = $1; }
 | term GEQ term { $$ = f_new_inst(); $$->code = P('<','=');     $$->a1.p = $3; $$->a2.p = $1; }
 | term '~' term     { $$ = f_new_inst(); $$->code = '~';        $$->a1.p = $1; $$->a2.p = $3; }
 | DEFINED '(' term ')' { $$ = f_new_inst(); $$->code = P('d','e');  $$->a1.p = $3; }

 | constant { $$ = $1; }
 | SYM {
     $$ = f_new_inst();
     switch ($1->class) {
       case SYM_VARIABLE | T_INT:
       case SYM_VARIABLE | T_PAIR:
       case SYM_VARIABLE | T_PREFIX:
       case SYM_VARIABLE | T_IP:
	 $$->code = 'C';
	 $$->a1.p = $1->aux2;
	 break;
       default:
	 cf_error("Can not use this class of symbol (%s,%x) as variable.", $1->name, $1->class );
     }
   }

 | rtadot FROM    { $$ = f_new_inst(); $$->code = 'a'; $$->aux = T_IP; $$->a2.i = OFFSETOF(struct rta, from); }

 | rtadot GW      { $$ = f_new_inst(); $$->code = 'a'; $$->aux = T_IP; $$->a2.i = OFFSETOF(struct rta, gw); }
 | rtadot NET     { $$ = f_new_inst(); $$->code = 'a'; $$->aux = T_PREFIX; $$->a2.i = 0x12345678; }
 | rtadot SOURCE  { $$ = f_new_inst(); $$->code = 'a'; $$->aux = T_ENUM_RTS; $$->a2.i = OFFSETOF(struct rta, gw); }

 | rtadot dynamic_attr { $$ = $2; $$->code = P('e','a'); }

 | term '.' IP { $$ = f_new_inst(); $$->code = P('c','p'); $$->a1.p = $1; $$->aux = T_IP; }
 | term '.' LEN { $$ = f_new_inst(); $$->code = P('c','p'); $$->a1.p = $1; $$->aux = T_INT; }
 | term '.' MASK '(' term ')' { $$ = f_new_inst(); $$->code = P('i','M'); $$->a1.p = $1; $$->a2.p = $5; }
 | function_call { $$ = $1; /* 1 shift/reduce conflict */ }
 ;

break_command:
   QUITBIRD { $$ = F_QUITBIRD }
 | ACCEPT { $$ = F_ACCEPT }
 | REJECT { $$ = F_REJECT }
 | ERROR { $$ = F_ERROR }
 | PRINT { $$ = F_NOP }
 | PRINTN { $$ = F_NONL }
 ;

print_one:
   term { $$ = f_new_inst(); $$->code = 'p'; $$->a1.p = $1; $$->a2.p = NULL; }
 ;

print_list: /* EMPTY */ { $$ = NULL; }
 | print_one print_list {
     if ($1) {
       $1->next = $2;
       $$ = $1;
     } else $$ = $2;
   }
 ;

var_listn: term { 
     $$ = f_new_inst();
     $$->code = 's';
     $$->a1.p = NULL;
     $$->a2.p = $1;
     $$->next = NULL;
   }
 | term ',' var_listn {
     $$ = f_new_inst();
     $$->code = 's';
     $$->a1.p = NULL;
     $$->a2.p = $1;
     $$->next = $3;
   }
 ;

var_list: /* EMPTY */ { $$ = NULL; }
 | var_listn { $$ = $1; }
 ;

cmd:
   IF term THEN block {
     $$ = f_new_inst();
     $$->code = '?';
     $$->a1.p = $2;
     $$->a2.p = $4;
   }
 | IF term THEN block ELSE block {
     struct f_inst *i = f_new_inst();
     i->code = '?';
     i->a1.p = $2;
     i->a2.p = $4;
     $$ = f_new_inst();
     $$->code = '?';
     $$->a1.p = i;
     $$->a2.p = $6;
   }
 | SYM '=' term ';' {
     $$ = f_new_inst();
     DBG( "Ook, we'll set value\n" );
     if (($1->class & ~T_MASK) != SYM_VARIABLE)
       cf_error( "You may only set variables, and this is %x.\n", $1->class );
     $$->code = 's';
     $$->a1.p = $1;
     $$->a2.p = $3;
   }
 | RETURN term ';' {
     $$ = f_new_inst();
     DBG( "Ook, we'll return the value\n" );
     $$->code = 'r';
     $$->a1.p = $2;
   }
 | rtadot dynamic_attr '=' term ';' {
     $$ = $2;
     $$->code  = P('e','S');
     $$->a1.p = $4;
   }
 | UNSET '(' rtadot dynamic_attr ')' ';' {
     $$ = $4;
     $$->aux = EAF_TYPE_UNDEF | EAF_INLINE;
     $$->code = P('e','S');
     $$->a1.p = NULL;
   }
 | break_command print_list ';' { $$ = f_new_inst(); $$->code = P('p',','); $$->a1.p = $2; $$->a2.i = $1; }
 | function_call ';' { $$ = $1; }
 | CASE term '{' switch_body '}' {
      $$ = f_new_inst();
      $$->code = P('S','W');
      $$->a1.p = $2;
      $$->a2.p = build_tree( $4 );
   }
 ;

CF_END
