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
#include "filter/filter.h"

CF_DECLS

%union {
  int i;
  ip_addr a;
  struct symbol *s;
  char *t;
  struct f_instruction *x; 
}

%token END
%token <i> NUM
%token <a> IPA
%token <s> SYM
%token <t> TEXT

%type <i> cexpr bool pxlen
%type <x> expr

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

/* Constant expressions */

cexpr:
   NUM
 | cexpr '+' cexpr { $$ = $1 + $3; }
 | cexpr '-' cexpr { $$ = $1 - $3; }
 | cexpr '*' cexpr { $$ = $1 * $3; }
 | cexpr '/' cexpr { if ($3) $$ = $1 / $3; else cf_error("Division by zero"); }
 | cexpr '%' cexpr { if ($3) $$ = $1 % $3; else cf_error("Division by zero"); }
 | '(' cexpr ')' { $$ = $2; }
 | SYM { if ($1->class != SYM_NUMBER) cf_error("Number expected"); else $$ = $1->aux; }
 ;

CF_ADDTO(conf, definition)
definition:
   DEFINE SYM '=' cexpr {
     if ($2->class != SYM_VOID) cf_error("Symbol already defined");
     $2->class = SYM_NUMBER;
     $2->aux = $4;
   }
 ;

/* Switches */

bool:
   cexpr {$$ = !!$1; }
 | ON { $$ = 1; }
 | YES { $$ = 1; }
 | OFF { $$ = 0; }
 | NO { $$ = 0; }
 | /* Silence means agreement */ { $$ = 1; }
 ;

/* Prefixes and netmasks */

pxlen:
   '/' NUM {
     if ($2 < 0 || $2 > 32) cf_error("Invalid prefix length %d", $2);
     $$ = $2;
   }
 | ':' IPA {
     $$ = ipa_mklen($2);
     if ($$ < 0) cf_error("Invalid netmask %I", $2);
   }
 ;

CF_CODE

CF_END
