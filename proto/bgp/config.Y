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

CF_KEYWORDS(BGP, LOCAL, NEIGHBOR, AS)

CF_GRAMMAR

CF_ADDTO(proto, bgp_proto '}' { bgp_check(BGP_CFG); } )

bgp_proto_start: proto_start BGP {
     this_proto = proto_config_new(&proto_bgp, sizeof(struct bgp_config));
     this_proto->preference = DEF_PREF_BGP;
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
 ;

CF_CODE

CF_END
