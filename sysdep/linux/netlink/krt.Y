/*
 *	BIRD -- Netlink Interface Configuration
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

#include "lib/krt.h"

#define KRT_PROTO ((struct krt_config *) this_proto)

CF_DECLS

CF_KEYWORDS(KERNEL, PERSIST, ROUTE, SCAN, TIME, LEARN)

CF_GRAMMAR

/* Kernel protocol */

CF_ADDTO(proto, kern_proto '}')

kern_proto_start: proto_start KERNEL {
     if (!(this_proto = cf_krt)) cf_error("Kernel protocol already defined");
     cf_krt = NULL;
   }
 ;

kern_proto:
   kern_proto_start '{'
 | kern_proto proto_item ';'
 | kern_proto kern_item ';'
 ;

kern_item:
   PERSIST bool { KRT_PROTO->persist = $2; }
 | SCAN TIME expr { KRT_PROTO->scan_time = $3; }
 | LEARN bool { KRT_PROTO->learn = $2; }
 | ROUTE SCAN TIME expr { KRT_PROTO->route_scan_time = $4; }
 ;

CF_CODE

CF_END
