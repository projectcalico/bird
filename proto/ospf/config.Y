/*
 *	BIRD -- OSPF Configuration
 *
 *	(c) 1999-2000 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

#include "proto/ospf/ospf.h"

CF_DEFINES

#define OSPF_CFG ((struct ospf_config *) this_proto)
static struct ospf_area_config *this_area;
static struct iface_patt *this_ipatt;
#define OSPF_PATT ((struct ospf_iface_patt *) this_ipatt)

CF_DECLS

CF_KEYWORDS(OSPF, AREA, OSPF_METRIC1, OSPF_METRIC2, OSPF_TAG)
CF_KEYWORDS(NEIGHBORS, RFC1583COMPAT, STUB, TICK, COST, RETRANSMIT)
CF_KEYWORDS(HELLO, TRANSIT, PRIORITY, DEAD, NONBROADCAST, POINTOPOINT, TYPE)

%type <t> opttext

CF_GRAMMAR

CF_ADDTO(proto, ospf_proto '}')

ospf_proto_start: proto_start OSPF {
     this_proto = proto_config_new(&proto_ospf, sizeof(struct ospf_config));
     this_proto->preference = DEF_PREF_OSPF;
     init_list(&OSPF_CFG->area_list);
     OSPF_CFG->rfc1583=1;
  }
 ;

ospf_proto:
   ospf_proto_start proto_name '{'
 | ospf_proto proto_item ';'
 | ospf_proto RFC1583COMPAT bool ';' { OSPF_CFG->rfc1583 = $3; }
 | ospf_proto ospf_area '}'
;

ospf_area_start: AREA idval '{' {
  this_area = cfg_allocz(sizeof(struct ospf_area_config));
  add_tail(&OSPF_CFG->area_list, NODE this_area);
  this_area->areaid = $2;
  this_area->tick = DISPTICK;
  this_area->stub = 0;
  init_list(&this_area->patt_list);
 }
;

ospf_area: /* EMPTY */
 | ospf_area_start ospf_area_opts
;

ospf_area_opts:
 | ospf_area_opts ospf_area_item

ospf_area_item:
 | STUB bool ';' { this_area->stub = $2 ; }
 | TICK NUM ';' { this_area->tick = $2 ; }
 | ospf_iface_list '}'
;

ospf_iface_item:
 | COST NUM { OSPF_PATT->cost = $2 ; }
 | HELLO NUM { OSPF_PATT->helloint = $2 ; }
 | RETRANSMIT NUM { OSPF_PATT->rxmtint = $2 ; }
 | TRANSIT DELAY NUM { OSPF_PATT->inftransdelay = $3 ; }
 | PRIORITY NUM { OSPF_PATT->priority = $2 ; }
 | WAIT NUM { OSPF_PATT->waitint = $2 ; }
 | DEAD COUNT NUM { OSPF_PATT->deadc = $3 ; }
 | TYPE BROADCAST { OSPF_PATT->type = OSPF_IT_BCAST ; }
 | TYPE NONBROADCAST { OSPF_PATT->type = OSPF_IT_NBMA ; }
 | TYPE POINTOPOINT { OSPF_PATT->type = OSPF_IT_PTP ; }
 | 
;
 
ospf_iface_start:
 {
  this_ipatt = cfg_allocz(sizeof(struct ospf_iface_patt));
  add_tail(&this_area->patt_list, NODE this_ipatt);
  OSPF_PATT->cost = COST_D;
  OSPF_PATT->helloint = HELLOINT_D;
  OSPF_PATT->rxmtint = RXMTINT_D;
  OSPF_PATT->inftransdelay = INFTRANSDELAY_D;
  OSPF_PATT->priority = PRIORITY_D;
  OSPF_PATT->waitint = WAIT_DMH*HELLOINT_D;
  OSPF_PATT->deadc = DEADC_D;
  OSPF_PATT->type = OSPF_IT_UNDEF;
 }
;

ospf_iface_opts:
   '{'
 | ospf_iface_opts ospf_iface_item ';'
 ;

ospf_iface_opt_list: /* EMPTY */ | ospf_iface_opts
;

ospf_iface:
  ospf_iface_start iface_patt ospf_iface_opt_list
;

ospf_iface_list:
   INTERFACE ospf_iface
 | ospf_iface_list ',' ospf_iface
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
