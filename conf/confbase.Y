/*
 *	BIRD -- Configuration Parser Top
 *
 *	(c) 1998--2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

#include "nest/bird.h"
#include "conf/conf.h"
#include "lib/resource.h"
#include "lib/socket.h"
#include "lib/timer.h"
#include "lib/string.h"
#include "nest/protocol.h"
#include "nest/iface.h"
#include "nest/route.h"
#include "nest/cli.h"
#include "filter/filter.h"

/* FIXME: Turn on YYERROR_VERBOSE and work around lots of bison bugs? */

CF_DECLS

%union {
  int i;
  u32 i32;
  ip_addr a;
  struct symbol *s;
  char *t;
  struct rtable_config *r;
  struct f_inst *x;
  struct filter *f;
  struct f_tree *e;
  struct f_val v;
  struct password_item *p;
  struct rt_show_data *ra;
  void *g;
  bird_clock_t time;
}

%token END CLI_MARKER INVALID_TOKEN
%token GEQ LEQ NEQ
%token <i> NUM ENUM
%token <i32> RTRID
%token <a> IPA
%token <s> SYM
%token <t> TEXT

%type <i> expr bool pxlen
%type <time> datetime

%nonassoc '=' '<' '>' '~' '.' GEQ LEQ NEQ
%left '+' '-'
%left '*' '/' '%'
%left '!'

CF_KEYWORDS(DEFINE, ON, OFF, YES, NO)

CF_GRAMMAR

/* Basic config file structure */

config: conf_entries END { return 0; }
 | CLI_MARKER cli_cmd { return 0; }
 ;

conf_entries:
   /* EMPTY */
 | conf_entries conf
 ;

CF_ADDTO(conf, ';')

/* Constant expressions */

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
   DEFINE SYM '=' expr ';' {
     cf_define_symbol($2, SYM_NUMBER, NULL);
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

datetime:
   TEXT {
     $$ = tm_parse_date($1);
     if (!$$)
       cf_error("Invalid date");
   }
 ;

CF_CODE

CF_END
