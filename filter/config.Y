/*
 *	BIRD - filters
 *
 *	Copyright 1998,1999 Pavel Machek
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 *
	FIXME: prefix variables, make prefix.ip and prefix.length work
	FIXME: define keyword
	FIXME: case without { }'s
	FIXME: allow px+, px- px^pair in prefix sets
	FIXME: create ip.mask(x) function
	FIXME: whole system of paths, path ~ string, path.prepend(), path.originate
	FIXME: create community lists
	FIXME: access to dynamic attributes
	FIXME: do not allow function call by callme(1,2,)
	FIXME: pairs of integers: define compare
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

CF_KEYWORDS(FUNCTION, PRINT, CONST,
	ACCEPT, REJECT, ERROR, QUITBIRD,
	INT, BOOL, IP, PREFIX, PAIR, SET, STRING,
	IF, THEN, ELSE, CASE,
	TRUE, FALSE,
	RTA, FROM, GW, NET,
	LEN,
	IMPOSSIBLE,
	FILTER
	)

%type <x> term block cmds cmd function_body ifthen constant print_one print_list var_list switch_body
%type <f> filter filter_body
%type <i> type break_command
%type <e> set_item set_items
%type <v> set_atom
%type <s> decls function_params 

CF_GRAMMAR

CF_ADDTO(conf, filter_def)
filter_def:
   FILTER SYM filter_body {
     cf_define_symbol($2, SYM_FILTER, $3);
     $3->name = $2->name;
     printf( "We have new filter defined (%s)\n", $2->name )
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

decls: /* EMPTY */ { $$ = NULL; }
 | type SYM ';' decls {
     cf_define_symbol($2, SYM_VARIABLE | $1, NULL);
     printf( "New variable %s type %x\n", $2->name, $1 );
     $2->aux = $4;
     $$=$2;
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
   '(' decls ')' { printf( "Have function parameters\n" ); $$=$2; }
 ;

function_body:
   decls '{' cmds '}' {
     $$ = $3;
   }
 ;

CF_ADDTO(conf, function_def)
function_def:
   FUNCTION SYM function_params function_body {
     extern struct f_inst *startup_func;
     cf_define_symbol($2, SYM_FUNCTION, $4);
     if (!strcasecmp($2->name, "startup"))
	startup_func = $4;
     $2->aux = $3;
     $2->aux2 = $4;
     printf("Hmm, we've got one function here - %s\n", $2->name); 
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

set_atom:
   NUM { $$.type = T_INT; $$.val.i = $1; }
 | IPA { $$.type = T_IP; $$.val.ip = $1; }
 
 ; 

set_item:
   set_atom { $$ = f_new_tree(); $$->from = $$->to = $1 }
 | set_atom '.' '.' set_atom { $$ = f_new_tree(); $$->from = $1; $$->to = $4; }
 ;

set_items:
   set_item { $$ = $1; }
 | set_items ',' set_item { $$ = $3; $$->left = $1; }
 ;

constant:
   CONST '(' expr ')' { $$ = f_new_inst(); $$->code = 'c'; $$->a1.i = T_INT; $$->a2.i = $3; }
 | NUM    { $$ = f_new_inst(); $$->code = 'c'; $$->a1.i = T_INT;  $$->a2.i = $1; }
 | TRUE   { $$ = f_new_inst(); $$->code = 'c'; $$->a1.i = T_BOOL; $$->a2.i = 1;  }
 | FALSE  { $$ = f_new_inst(); $$->code = 'c'; $$->a1.i = T_BOOL; $$->a2.i = 0;  }
 | TEXT   { $$ = f_new_inst(); $$->code = 'c'; $$->a1.i = T_STRING; $$->a2.p = $1; }
 | '(' NUM ',' NUM ')' { $$ = f_new_inst(); $$->code = 'c'; $$->a1.i = T_PAIR;  $$->a2.i = $2 << 16 | $4; }
 | IPA	  { struct f_val * val; val = cfg_alloc(sizeof(struct f_val)); $$ = f_new_inst(); $$->code = 'C'; $$->a1.p = val; val->type = T_IP; val->val.ip = $1; }
 | IPA '/' NUM { struct f_val * val; val = cfg_alloc(sizeof(struct f_val)); $$ = f_new_inst(); $$->code = 'C'; $$->a1.p = val; val->type = T_PREFIX; val->val.px.ip = $1; val->val.px.len = $3; printf( "ook, we have prefix here\n" ); }
 | '[' set_items ']' { printf( "We've got a set here..." ); $$ = f_new_inst(); $$->code = 'c'; $$->a1.i = T_SET; $$->a2.p = build_tree($2); printf( "ook\n" ); }
 ;

term:
   term '+' term     { $$ = f_new_inst(); $$->code = '+';  $$->a1.p = $1; $$->a2.p = $3; }
 | term '=' term     { $$ = f_new_inst(); $$->code = '=='; $$->a1.p = $1; $$->a2.p = $3; }
 | term '!' '=' term { $$ = f_new_inst(); $$->code = '!='; $$->a1.p = $1; $$->a2.p = $4; }
 | term '<' term     { $$ = f_new_inst(); $$->code = '<';  $$->a1.p = $1; $$->a2.p = $3; }
 | term '<' '=' term { $$ = f_new_inst(); $$->code = '<='; $$->a1.p = $1; $$->a2.p = $4; }
 | term '>' term     { $$ = f_new_inst(); $$->code = '<';  $$->a1.p = $3; $$->a2.p = $1; }
 | term '>' '=' term { $$ = f_new_inst(); $$->code = '<='; $$->a1.p = $4; $$->a2.p = $1; }
 | term '~' term     { $$ = f_new_inst(); $$->code = '~';  $$->a1.p = $1; $$->a2.p = $3; }

 | SYM {
     $$ = f_new_inst();
     switch ($1->class) {
       case SYM_VARIABLE | T_INT:
	 $$->code = 'i';
         $$->a1.i = T_INT;
	 $$->a2.p = &($1->aux);
	 break;
       default:
	 cf_error("Can not use this class of symbol as variable." );
     }
   }
 | constant { $$ = $1; }
 | RTA '.' FROM { $$ = f_new_inst(); $$->code = 'a'; $$->a1.i = T_IP; $$->a2.i = OFFSETOF(struct rta, from); }

 | RTA '.' GW { $$ = f_new_inst(); $$->code = 'a'; $$->a1.i = T_IP; $$->a2.i = OFFSETOF(struct rta, gw); }
 | RTA '.' NET  { $$ = f_new_inst(); $$->code = 'a'; $$->a1.i = T_PREFIX; $$->a2.i = 0x12345678; }

 | term '.' IP { $$ = f_new_inst(); $$->code = 'cp'; $$->a1.p = $1; $$->a2.i = T_IP; }
 | term '.' LEN { $$ = f_new_inst(); $$->code = 'cp'; $$->a1.p = $1; $$->a2.i = T_INT; }
 ;

break_command:
   QUITBIRD { $$ = F_QUITBIRD }
 | ACCEPT { $$ = F_ACCEPT }
 | REJECT { $$ = F_REJECT }
 | ERROR { $$ = F_ERROR }
 | PRINT { $$ = F_NOP }
 ;

ifthen:
   IF term THEN block {
     $$ = f_new_inst();
     $$->code = '?';
     $$->a1.p = $2;
     $$->a2.p = $4;
   }
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

var_list: /* EMPTY */ { $$ = NULL; }
 | term ',' var_list {
     $$ = f_new_inst();
     $$->code = 's';
     $$->a1.p = NULL;
     $$->a2.p = $1;
     $$->next = $3;
   }
 ;

switch_body: /* EMPTY */ { $$ = NULL; }
 | term ':' block switch_body {
     $$ = f_new_inst();
     $$->code = 'of';
     $$->a1.p = $1;
     $$->a2.p = $3;
     $$->next = $4;
   }
 | ELSE ':' block {
     $$ = f_new_inst();
     $$->code = 'el';
     $$->a1.p = NULL;
     $$->a2.p = $3;
   }
 ;

cmd:
   ifthen { 
     $$ = $1;
   }
	/* FIXME: this leads to shift/reduce conflict. */
 | ifthen ELSE block {
     $$ = f_new_inst();
     $$->code = '?';
     $$->a1.p = $1;
     $$->a2.p = $3;
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
      $$->code = 'sw';
      $$->a1.p = $2;
      $$->a2.p = $4;
   }
 ;

CF_END
