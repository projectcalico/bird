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

CF_KEYWORDS(FUNCTION, FILTER, PRINTDEBUG, INT, PRINT)

%type <x> term

CF_GRAMMAR

program: /* EMPTY */
 | program function
 ;

CF_ADDTO(conf, function)
function:
   FUNCTION SYM '(' ')' '{' term '}' { 
     extern struct f_instruction *last_func;
     if ($2->class != SYM_VOID) cf_error("Symbol already defined" );
     $2->class = SYM_FUNCTION;
     $2->aux = $6;
     last_func = $6;
     printf("Hmm, we've got one function here\n"); 
   }
 ;

CF_ADDTO(conf, filter)
filter:
   FILTER SYM '{' term '}' {
     if ($2->class != SYM_VOID) cf_error("Symbol already defined" );
     $2->class = SYM_FILTER;
     $2->aux = $4;
     printf( "We have new filter defined (%s)\n", $2->name )
   }
 ;

/* Programs */

term: /* EMPTY */ { $$ = NULL; }
 | term ';' term { 
     $$ = cfg_alloc(sizeof(struct f_instruction));
     printf( "We've got statement here\n" );
     $$->code = ',';
     $$->arg1 = $1;
     $$->arg2 = $3;
   }
 | INT SYM {
     if ($2->class != SYM_VOID) cf_error("Symbol already defined, can not use as variable\n" );
     $2->class = SYM_VARIABLE_INT;
     printf( "New variable\n" );
     $$ = NULL;
   }
 | SYM '=' expr {
     $$ = cfg_alloc(sizeof(struct f_instruction));
     printf( "Ook, we'll set value\n" );
     if ($1->class != SYM_VARIABLE_INT)
       cf_error( "You may only set variables\n" );
     $$->code = '=';
     $$->arg1 = $1;
     $$->arg2 = $3;
   }
 | PRINT '(' SYM ')' {
     $$ = cfg_alloc(sizeof(struct f_instruction));
     printf( "Ook, we'll print something\n" );
     $$->code = 'p';
     $$->arg1 = $3;
     $$->arg2 = NULL;
   }
 | PRINTDEBUG {
     $$ = cfg_alloc(sizeof(struct f_instruction));
     $$->code = 'D';
     $$->arg1 = $$->arg2 = NULL;
 }
 ;

CF_END
