/*
 *	BIRD -- Static Protocol Configuration
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

#include "proto/static/static.h"

CF_DECLS

CF_KEYWORDS(STATIC)

CF_GRAMMAR

CF_ADDTO(proto, static_proto '}')

static_proto_start: proto_start STATIC {
     this_proto = proto_new(&proto_static, sizeof(struct static_proto));
     static_init_instance((struct static_proto *) this_proto);
  }
 ;

static_proto:
   static_proto_start proto_name '{'
 | static_proto proto_item ';'
 ;

CF_CODE

CF_END
