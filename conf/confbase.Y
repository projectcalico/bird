/*
 *	BIRD -- Configuration Parser Top
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

#include "nest/bird.h"
#include "conf/conf.h"
#include "lib/resource.h"
#include "lib/socket.h"
#include "lib/timer.h"
#include "nest/protocol.h"
#include "nest/iface.h"
#include "nest/route.h"

CF_DECLS

%union {
  int i;
  ip_addr a;
  struct symbol *s;
  char *t;
}

%token END
%token <i> NUM
%token <a> IPA
%token <s> SYM
%token <t> TEXT

%type <i> expr bool

%left '+' '-'
%left '*' '/' '%'

CF_KEYWORDS(DEFINE, ON, OFF, YES, NO)

CF_GRAMMAR

/* Basic config file structure */

config: conf_entries END {
	return 0;
   }
 ;

conf_entries:
   /* EMPTY */
 | conf_entries conf ';'
 ;

CF_ADDTO(conf, /* EMPTY */)

/* Expressions */

expr:
   NUM
 | expr '+' expr { $$ = $1 + $3; }
 | expr '-' expr { $$ = $1 - $3; }
 | expr '*' expr { $$ = $1 * $3; }
 | expr '/' expr { if ($3) $$ = $1 / $3; else cf_error("Division by zero"); }
 | expr '%' expr { if ($3) $$ = $1 % $3; else cf_error("Division by zero"); }
 | '(' expr ')' { $$ = $2; }
 | SYM { if ($1->class != SYM_NUMBER) cf_error("Number expected"); else $$ = $1->aux; }
 ;

CF_ADDTO(conf, definition)
definition:
   DEFINE SYM '=' expr {
     if ($2->class != SYM_VOID) cf_error("Symbol already defined");
     $2->class = SYM_NUMBER;
     $2->aux = $4;
   }
 ;

/* Switches */

bool:
   expr {$$ = !!$1; }
 | ON { $$ = 1; }
 | YES { $$ = 1; }
 | OFF { $$ = 0; }
 | NO { $$ = 0; }
 | /* Silence means agreement */ { $$ = 1; }
 ;

CF_CODE

CF_END
