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

CF_KEYWORDS(OSPF, AREA, OSPF_METRIC1, OSPF_METRIC2, OSPF_TAG)
CF_KEYWORDS(NEIGHBORS)

%type <t> opttext

CF_GRAMMAR

CF_ADDTO(proto, ospf_proto '}')

ospf_proto_start: proto_start OSPF {
     this_proto = proto_config_new(&proto_ospf, sizeof(struct ospf_config));
     this_proto->preference = DEF_PREF_OSPF;
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

opttext:
    TEXT
| /* empty */ { $$ = NULL; }
;

CF_ADDTO(dynamic_attr, OSPF_METRIC1 { $$ = f_new_dynamic_attr(EAF_TYPE_INT | EAF_TEMP, T_INT, EA_OSPF_METRIC1); })
CF_ADDTO(dynamic_attr, OSPF_METRIC2 { $$ = f_new_dynamic_attr(EAF_TYPE_INT | EAF_TEMP, T_INT, EA_OSPF_METRIC2); })
CF_ADDTO(dynamic_attr, OSPF_TAG { $$ = f_new_dynamic_attr(EAF_TYPE_INT | EAF_TEMP, T_INT, EA_OSPF_TAG); })

CF_CLI(SHOW OSPF, optsym, [<name>], [[Show information about ospf protocol]])
{ ospf_sh(proto_get_named($3, &proto_ospf)); } ;

CF_CLI(SHOW OSPF NEIGHBORS, optsym opttext, [<name>] [\"<interface>\"], [[Show information about ospf neighbors]])
{ ospf_sh_neigh(proto_get_named($4, &proto_ospf), $5); } ;

CF_CLI(SHOW OSPF INTERFACE, optsym opttext, [<name>] [\"<interface>\"], [[Show infomation about interface]])
{ ospf_sh_iface(proto_get_named($4, &proto_ospf), $5); };

CF_CODE

CF_END
