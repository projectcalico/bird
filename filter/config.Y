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
	IF, THEN,
	FILTER
	)

%type <x> term block cmds cmd function_body
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

term:
   term '+' term {
     $$ = f_new_inst();
     $$->code = '+';
     $$->arg1 = $1;
     $$->arg2 = $3;
   }
 | SYM {
     $$ = f_new_inst();
     switch ($1->class) {
       case SYM_VARIABLE | T_INT:
	 $$->code = 'i';
         $$->arg1 = &($1->aux);
	 break;
       default:
	 cf_error("Can not use this class of symbol as variable" );
     }
   }
 | CONST '(' expr ')' {
     $$ = f_new_inst();
     $$->code = 'c';
     $$->arg1 = $3;
   }
 | NUM {
     $$ = f_new_inst();
     $$->code = 'c';
     $$->arg1 = $1
   }
 ;

break_command:
   QUITBIRD { $$ = F_QUITBIRD }
 | ACCEPT { $$ = F_ACCEPT }
 | REJECT { $$ = F_REJECT }
 | ERROR { $$ = F_ERROR }
 ;

cmd:
   IF term THEN block {
     $$ = f_new_inst();
     $$->code = '?';
     $$->arg1 = $2;
     $$->arg2 = $4;
   }
 | SYM '=' term ';' {
     $$ = f_new_inst();
     printf( "Ook, we'll set value\n" );
     if (($1->class & ~T_MASK) != SYM_VARIABLE)
       cf_error( "You may only set variables, and this is %x.\n", $1->class );
     $$->code = '=';
     $$->arg1 = $1;
     $$->arg2 = $3;
   }
 | PRINT '(' term ')' ';' {
     $$ = f_new_inst();
     printf( "Ook, we'll print something\n" );
     $$->code = 'p';
     $$->arg1 = $3;
     $$->arg2 = NULL;
   }
 | PUTS '(' TEXT ')' ';' {
     $$ = f_new_inst();
     $$->code = 'd';
     $$->arg1 = $3;
   }
 | PRINTDEBUG ';' {
     $$ = f_new_inst();
     $$->code = 'D';
     $$->arg1 = $$->arg2 = NULL;
   }
 | break_command ';' {
     $$ = f_new_inst();
     $$->code = '!';
     (int) $$->arg1 = $1;
   }
 ;

CF_END
