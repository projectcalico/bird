/*
 *	BIRD - filters
 *
 *	Copyright 1998 Pavel Machek
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
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

CF_KEYWORDS(FUNCTION, PRINTDEBUG, PRINT, CONST, PUTS, 
	ACCEPT, REJECT, ERROR, QUITBIRD,
	INT, BOOL, IP, PREFIX, PAIR, SET, STRING,
	IF, THEN, ELSE,
	TRUE, FALSE,
	FILTER
	)

%type <x> term block cmds cmd function_body ifthen constant print_one print_list
%type <f> filter filter_body
%type <i> type break_command

CF_GRAMMAR

CF_ADDTO(conf, filter_def)
filter_def:
   FILTER SYM filter_body {
     if ($2->class != SYM_VOID) cf_error("Symbol already defined" );
     $2->class = SYM_FILTER;
     $2->def = $3;
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

decls: /* EMPTY */
 | type SYM ';' decls {
     if ($2->class != SYM_VOID) cf_error("Symbol already defined, can not use as variable\n" );
     $2->class = SYM_VARIABLE | $1;
     printf( "New variable %s type %x\n", $2->name, $1 );
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
   '(' decls ')' { printf( "Have function parameters\n" ); }
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
     if ($2->class != SYM_VOID) cf_error("Symbol already defined" );
     $2->class = SYM_FUNCTION;
     $2->def = $4;
     if (!strcasecmp($2->name, "startup"))
	startup_func = $4;
     printf("Hmm, we've got one function here - %s\n", $2->name); 
   }
 ;

/* Programs */

cmds: /* EMPTY */ { $$ = NULL; }
 | cmd cmds {
     if ($1) {
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

constant:
   CONST '(' expr ')' { $$ = f_new_inst(); $$->code = 'c'; $$->a1.i = T_INT; $$->a2.i = $3; }
 | NUM    { $$ = f_new_inst(); $$->code = 'c'; $$->a1.i = T_INT;  $$->a2.i = $1; }
 | TRUE   { $$ = f_new_inst(); $$->code = 'c'; $$->a1.i = T_BOOL; $$->a2.i = 1;  }
 | FALSE  { $$ = f_new_inst(); $$->code = 'c'; $$->a1.i = T_BOOL; $$->a2.i = 0;  }
 | TEXT   { $$ = f_new_inst(); $$->code = 'c'; $$->a1.i = T_STRING; $$->a2.p = $1; } 
 ;

term:
   term '+' term     { $$ = f_new_inst(); $$->code = '+';  $$->a1.p = $1; $$->a2.p = $3; }

 | term '=' term     { $$ = f_new_inst(); $$->code = '=='; $$->a1.p = $1; $$->a2.p = $3; }
 | term '!' '=' term { $$ = f_new_inst(); $$->code = '!='; $$->a1.p = $1; $$->a2.p = $4; }
 | term '<' term     { $$ = f_new_inst(); $$->code = '<';  $$->a1.p = $1; $$->a2.p = $3; }
 | term '<' '=' term { $$ = f_new_inst(); $$->code = '<='; $$->a1.p = $1; $$->a2.p = $4; }
 | term '>' term     { $$ = f_new_inst(); $$->code = '<';  $$->a1.p = $3; $$->a2.p = $1; }
 | term '>' '=' term { $$ = f_new_inst(); $$->code = '<='; $$->a1.p = $4; $$->a2.p = $1; }

 | SYM {
     $$ = f_new_inst();
     switch ($1->class) {
       case SYM_VARIABLE | T_INT:
	 $$->code = 'i';
         $$->a1.i = T_INT;
	 $$->a2.p = &($1->aux);
	 break;
       default:
	 cf_error("Can not use this class of symbol as variable" );
     }
   }
 | constant { $$ = $1; }
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
 ;

CF_END
