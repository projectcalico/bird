/*
 *	BIRD -- RIP Configuration
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

#include "proto/rip/rip.h"

CF_DECLS

CF_KEYWORDS(RIP)

CF_GRAMMAR

CF_ADDTO(proto, rip_proto '}')

rip_proto_start: proto_start RIP {
     this_proto = proto_new(&proto_rip, sizeof(struct rip_data));
     rip_init_instance(this_proto);
   }
 ;

rip_proto:
   rip_proto_start proto_name '{'
 | rip_proto proto_item ';'
 ;

CF_CODE

CF_END
