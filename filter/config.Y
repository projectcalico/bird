/*
 *	BIRD - filters
 *
 *	Copyright 1998 Pavel Machek
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

#include "nest/bird.h"
#include "filter/filter.h"
#include "lib/resource.h"
#include "lib/socket.h"
#include "lib/timer.h"
#include "nest/protocol.h"
#include "nest/iface.h"
#include "nest/route.h"

CF_DECLS

CF_KEYWORDS(FUNCTION, FILTER, PRINTDEBUG, INT, PRINT, CONST, VAR, PUTS, DIE, IF)

%type <x> term
%type <x> block
%type <x> cmds

CF_GRAMMAR

CF_ADDTO(conf, function)
function:
   FUNCTION SYM '(' ')' '{' cmds '}' { 
     extern struct f_inst *last_func;
     if ($2->class != SYM_VOID) cf_error("Symbol already defined" );
     $2->class = SYM_FUNCTION;
     $2->def = $6;
     last_func = $6;
     printf("Hmm, we've got one function here\n"); 
   }
 ;

CF_ADDTO(conf, filter)
filter:
   FILTER SYM '{' cmds '}' {
     if ($2->class != SYM_VOID) cf_error("Symbol already defined" );
     $2->class = SYM_FILTER;
     $2->def = $4;
     printf( "We have new filter defined (%s)\n", $2->name )
   }
 ;

/* Programs */

cmds:
   term {
     if ($1) {
       $1->next = NULL;
       $$ = $1;
     } else $$ = NULL;
   }
 | term ';' cmds {
     if ($1) {
       $1->next = $3;
       $$ = $1;
     } else $$ = $3;
   }
 ;

block:
   term ';' {
     $$=$1;
   }
 | '{' cmds '}' {
     $$=$2;
   }
 ;

term:
   /* EMPTY */ { 
     $$ = NULL;
   }
 | term '+' term {
     $$ = f_new_inst();
     $$->code = '+';
     $$->arg1 = $1;
     $$->arg2 = $3;
   }
 | IF '(' term ')' block {
     $$ = f_new_inst();
     $$->code = '?';
     $$->arg1 = $3;
     $$->arg2 = $5;
   }
 | INT SYM {
     if ($2->class != SYM_VOID) cf_error("Symbol already defined, can not use as variable\n" );
     $2->class = SYM_VARIABLE_INT;
     printf( "New variable\n" );
     $$ = NULL;
   }
 | SYM {
     $$ = f_new_inst();
     switch ($1->class) {
       case SYM_VARIABLE_INT:
	 $$->code = 'i';
         $$->arg1 = &($1->aux);
	 break;
       default:
	 cf_error("Can not use this class of symbol as variable" );
     }
   }
 | VAR '(' SYM ')' {
     $$ = f_new_inst();
     switch ($3->class) {
       case SYM_VARIABLE_INT:
	 $$->code = 'i';
         $$->arg1 = &($3->aux);
	 break;
       default:
	 cf_error("Can not use this class of symbol as variable" );
     }
   }
 | NUM {
     $$ = f_new_inst();
     $$->code = 'c';
     $$->arg1 = $1
   }
 | CONST '(' expr ')' {
     $$ = f_new_inst();
     $$->code = 'c';
     $$->arg1 = $3;
   }
 | SYM '=' term {
     $$ = f_new_inst();
     printf( "Ook, we'll set value\n" );
     if ($1->class != SYM_VARIABLE_INT)
       cf_error( "You may only set variables\n" );
     $$->code = '=';
     $$->arg1 = $1;
     $$->arg2 = $3;
   }
 | PRINT '(' term ')' {
     $$ = f_new_inst();
     printf( "Ook, we'll print something\n" );
     $$->code = 'p';
     $$->arg1 = $3;
     $$->arg2 = NULL;
   }
 | PUTS '(' TEXT ')' {
     $$ = f_new_inst();
     $$->code = 'd';
     $$->arg1 = $3;
   }
 | DIE {
     $$ = f_new_inst();
     $$->code = '!';
   }
 | PRINTDEBUG {
     $$ = f_new_inst();
     $$->code = 'D';
     $$->arg1 = $$->arg2 = NULL;
 }
 ;

CF_END
