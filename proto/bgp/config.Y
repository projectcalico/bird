/*
 *	BIRD -- Border Gateway Protocol Configuration
 *
 *	(c) 2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

#include "proto/bgp/bgp.h"

#define BGP_CFG ((struct bgp_config *) this_proto)

CF_DECLS

CF_KEYWORDS(BGP, LOCAL, NEIGHBOR, AS, HOLD, TIME, CONNECT, RETRY, KEEPALIVE,
	MULTIHOP, STARTUP, VIA, NEXT, HOP, SELF)

CF_GRAMMAR

CF_ADDTO(proto, bgp_proto '}' { bgp_check(BGP_CFG); } )

bgp_proto_start: proto_start BGP {
     this_proto = proto_config_new(&proto_bgp, sizeof(struct bgp_config));
     this_proto->preference = DEF_PREF_BGP;
     BGP_CFG->hold_time = 240;
     BGP_CFG->connect_retry_time = 120;
     BGP_CFG->initial_hold_time = 240;
  }
 ;

bgp_proto:
   bgp_proto_start proto_name '{'
 | bgp_proto proto_item ';'
 | bgp_proto LOCAL AS NUM ';' {
     if ($4 < 0 || $4 > 65535) cf_error("AS number out of range");
     BGP_CFG->local_as = $4;
   }
 | bgp_proto NEIGHBOR IPA AS NUM ';' {
     if ($5 < 0 || $5 > 65535) cf_error("AS number out of range");
     BGP_CFG->remote_ip = $3;
     BGP_CFG->remote_as = $5;
   }
 | bgp_proto HOLD TIME NUM ';' { BGP_CFG->hold_time = $4; }
 | bgp_proto STARTUP HOLD TIME NUM ';' { BGP_CFG->initial_hold_time = $5; }
 | bgp_proto CONNECT RETRY TIME NUM ';' { BGP_CFG->connect_retry_time = $5; }
 | bgp_proto KEEPALIVE TIME NUM ';' { BGP_CFG->connect_retry_time = $4; }
 | bgp_proto MULTIHOP NUM VIA IPA ';' { BGP_CFG->multihop = $3; BGP_CFG->multihop_via = $5; }
 | bgp_proto NEXT HOP SELF ';' { BGP_CFG->next_hop_self = 1; }
 ;

CF_CODE

CF_END
