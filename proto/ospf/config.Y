/*
 *	BIRD -- OSPF Configuration
 *
 *	(c) 1999 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

#include "proto/ospf/ospf.h"

CF_DECLS

CF_KEYWORDS(OSPF, AREA)

CF_GRAMMAR

CF_ADDTO(proto, ospf_proto '}')

ospf_proto_start: proto_start OSPF {
     this_proto = proto_config_new(&proto_ospf, sizeof(struct ospf_config));
  }
 ;

ospf_proto:
   ospf_proto_start proto_name '{'
   | ospf_proto proto_item ';'
 | ospf_proto ospf_area ';'
 ;

ospf_area: AREA idval {
    ((struct ospf_config *)this_proto)->area = $2;
 }
 ;

CF_CODE

CF_END
