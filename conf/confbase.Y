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

CF_GRAMMAR

config: conf_entries END {
	return 0;
   }
 ;

conf_entries:
   /* EMPTY */
 | conf_entries conf ';'
 ;

CF_ADDTO(conf, /* EMPTY */)

CF_CODE

CF_END
