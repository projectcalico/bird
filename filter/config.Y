/*
 *	BIRD - filters
 *
 *	Copyright 1998,1999 Pavel Machek
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 *
	FIXME (nonurgent): define keyword
	FIXME: whole system of paths, path ~ string, path.prepend(), path.originate
	FIXME: create community lists
	FIXME: enumerational types
	FIXME: write access to dynamic attributes.
 */

CF_HDR

#include "nest/bird.h"
#include "lib/resource.h"
#include "lib/socket.h"
#include "lib/timer.h"
#include "nest/protocol.h"
#include "nest/iface.h"
#include "nest/route.h"

CF_DECLS

CF_KEYWORDS(FUNCTION, PRINT, PRINTN, CONST,
	ACCEPT, REJECT, ERROR, QUITBIRD,
	INT, BOOL, IP, PREFIX, PAIR, SET, STRING,
	IF, THEN, ELSE, CASE,
	TRUE, FALSE,
	RTA, FROM, GW, NET, MASK, RIP_METRIC, RIP_TAG,
	LEN,
	DEFINED,
	IMPOSSIBLE,
	FILTER
	)

%nonassoc ELSE
%nonassoc THEN

%type <x> term block cmds cmd function_body constant print_one print_list var_list var_listn any_dynamic
%type <f> filter filter_body
%type <i> type break_command pair
%type <e> set_item set_items switch_body
%type <v> set_atom prefix prefix_s ipa
%type <s> decls declsn one_decl function_params 

CF_GRAMMAR

CF_ADDTO(conf, filter_def)
filter_def:
   FILTER SYM { cf_push_scope( $2->name ); } filter_body {
     cf_define_symbol($2, SYM_FILTER, $4);
     $4->name = $2->name;
     printf( "We have new filter defined (%s)\n", $2->name );
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
     printf( "New variable %s type %x\n", $2->name, $1 );
     $2->aux = NULL;
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
     $$->aux = $3;
   }
 ;

/* Declarations that have no ';' at the end. */
declsn: one_decl { $$ = $1; }
 | declsn ';' one_decl {
     $$ = $3;
     $$->aux = $1;
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

function_params:
   '(' declsn ')' { printf( "Have function parameters\n" ); $$=$2; }
 | '(' ')' { $$=NULL; }
 ;

function_body:
   decls '{' cmds '}' {
     $$ = $3;
   }
 ;

CF_ADDTO(conf, function_def)
function_def:
   FUNCTION SYM { printf( "Begining of function %s\n", $2->name ); cf_push_scope($2->name); } function_params function_body {
     extern struct f_inst *startup_func;
     cf_define_symbol($2, SYM_FUNCTION, $5);
     if (!strcasecmp($2->name, "startup"))
	startup_func = $5;
     $2->aux = $4;
     $2->aux2 = $5;
     printf("Hmm, we've got one function here - %s\n", $2->name); 
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
   IPA '/' NUM { $$.type = T_PREFIX; $$.val.px.ip = $1; $$.val.px.len = $3; printf( "ook, we have prefix here\n" ); }
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
   CONST '(' expr ')' { $$ = f_new_inst(); $$->code = 'c'; $$->a1.i = T_INT; $$->a2.i = $3; }
 | NUM    { $$ = f_new_inst(); $$->code = 'c'; $$->a1.i = T_INT;  $$->a2.i = $1; }
 | TRUE   { $$ = f_new_inst(); $$->code = 'c'; $$->a1.i = T_BOOL; $$->a2.i = 1;  }
 | FALSE  { $$ = f_new_inst(); $$->code = 'c'; $$->a1.i = T_BOOL; $$->a2.i = 0;  }
 | TEXT   { $$ = f_new_inst(); $$->code = 'c'; $$->a1.i = T_STRING; $$->a2.p = $1; }
 | pair   { $$ = f_new_inst(); $$->code = 'c'; $$->a1.i = T_PAIR;  $$->a2.i = $1; }
 | ipa	   { NEW_F_VAL; $$ = f_new_inst(); $$->code = 'C'; $$->a1.p = val; *val = $1; }
 | prefix  {NEW_F_VAL; $$ = f_new_inst(); $$->code = 'C'; $$->a1.p = val; *val = $1; }
 | '[' set_items ']' { printf( "We've got a set here..." ); $$ = f_new_inst(); $$->code = 'c'; $$->a1.i = T_SET; $$->a2.p = build_tree($2); printf( "ook\n" ); }
 ;

any_dynamic:
   RIP_METRIC { $$ = f_new_inst(); $$->a1.i = T_INT; $$->a2.i = EA_RIP_METRIC;}
 | RIP_TAG    { $$ = f_new_inst(); $$->a1.i = T_INT; $$->a2.i = EA_RIP_TAG; }
 ;


term:
   '(' term ')'      { $$ = $2; }
 | term '+' term     { $$ = f_new_inst(); $$->code = '+';  $$->a1.p = $1; $$->a2.p = $3; }
 | term '=' term     { $$ = f_new_inst(); $$->code = '=='; $$->a1.p = $1; $$->a2.p = $3; }
 | term '!' '=' term { $$ = f_new_inst(); $$->code = '!='; $$->a1.p = $1; $$->a2.p = $4; }
 | term '<' term     { $$ = f_new_inst(); $$->code = '<';  $$->a1.p = $1; $$->a2.p = $3; }
 | term '<' '=' term { $$ = f_new_inst(); $$->code = '<='; $$->a1.p = $1; $$->a2.p = $4; }
 | term '>' term     { $$ = f_new_inst(); $$->code = '<';  $$->a1.p = $3; $$->a2.p = $1; }
 | term '>' '=' term { $$ = f_new_inst(); $$->code = '<='; $$->a1.p = $4; $$->a2.p = $1; }
 | term '~' term     { $$ = f_new_inst(); $$->code = '~';  $$->a1.p = $1; $$->a2.p = $3; }
 | DEFINED '(' term ')' { $$ = f_new_inst(); $$->code = 'de';  $$->a1.p = $3; }

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
	 cf_error("Can not use this class of symbol as variable." );
     }
   }
 | constant { $$ = $1; }
 | RTA '.' FROM { $$ = f_new_inst(); $$->code = 'a'; $$->a1.i = T_IP; $$->a2.i = OFFSETOF(struct rta, from); }

 | RTA '.' GW { $$ = f_new_inst(); $$->code = 'a'; $$->a1.i = T_IP; $$->a2.i = OFFSETOF(struct rta, gw); }
 | RTA '.' NET { $$ = f_new_inst(); $$->code = 'a'; $$->a1.i = T_PREFIX; $$->a2.i = 0x12345678; }

 | RTA '.' any_dynamic { $$ = $3; $$->code = 'ea'; }

 | term '.' IP { $$ = f_new_inst(); $$->code = 'cp'; $$->a1.p = $1; $$->a2.i = T_IP; }
 | term '.' LEN { $$ = f_new_inst(); $$->code = 'cp'; $$->a1.p = $1; $$->a2.i = T_INT; }
 | term '.' MASK '(' term ')' { $$ = f_new_inst(); $$->code = 'iM'; $$->a1.p = $1; $$->a2.p = $5; }
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
     printf( "Ook, we'll set value\n" );
     if (($1->class & ~T_MASK) != SYM_VARIABLE)
       cf_error( "You may only set variables, and this is %x.\n", $1->class );
     $$->code = 's';
     $$->a1.p = $1;
     $$->a2.p = $3;
   }
 | break_command print_list ';' { $$ = f_new_inst(); $$->code = 'p,'; $$->a1.p = $2; $$->a2.i = $1; }
 | SYM '(' var_list ')' ';' {
     struct symbol *sym;
     struct f_inst *inst = $3;
     if ($1->class != SYM_FUNCTION)
       cf_error("You can not call something which is not function. Really.");
     printf("You are calling function %s\n", $1->name);
     $$ = f_new_inst();
     $$->code = 'ca';
     $$->a1.p = inst;
     $$->a2.p = $1->aux2;
     sym = $1->aux;
     while (sym || inst) {
       if (!sym || !inst)
	 cf_error("wrong number of arguments for function %s.", $1->name);
       printf( "You should pass parameter called %s\n", sym->name);
       inst->a1.p = sym;
       sym = sym->aux;
       inst = inst->next;
     }
   }
 | CASE term '{' switch_body '}' {
      $$ = f_new_inst();
      $$->code = 'SW';
      $$->a1.p = $2;
      $$->a2.p = build_tree( $4 );
   }
 ;

CF_END
