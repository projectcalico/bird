/*
 *	BIRD -- RIP Configuration
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

/*
To add:

passive option (== do not send routing updates to this interface)
version1 switch
multicast off option for interface

interface mode broadcast/multicast/quiet

*/


CF_HDR

#include "proto/rip/rip.h"
#include "nest/iface.h"

void rip_dev_add_iface(char *);
struct rip_patt *rip_get_iface(void);

#define RIP_CFG ((struct rip_proto_config *) this_proto)

CF_DECLS

CF_KEYWORDS(RIP, INFINITY, METRIC, PORT, PERIOD, GARBAGETIME, MODE, MULTICAST, BROADCAST, QUIET, DEFAULT)

%type <i> rip_mode

CF_GRAMMAR

CF_ADDTO(proto, RIP_CFG '}')

RIP_CFG_start: proto_start RIP {
     RIP_CFG = proto_config_new(&proto_rip, sizeof(struct rip_proto_config));
     rip_init_config(RIP_CFG);
   }
 ;

RIP_CFG:
   RIP_CFG_start proto_name '{'
 | RIP_CFG proto_item ';'
 | RIP_CFG INFINITY expr ';'	{ RIP_CFG->infinity = $3; }
 | RIP_CFG PORT expr ';'	{ RIP_CFG->port = $3; }
 | RIP_CFG PERIOD expr ';'	{ RIP_CFG->period = $3; }
 | RIP_CFG GARBAGETIME expr ';' { RIP_CFG->garbage_time = $3; }
 | RIP_CFG rip_iface_list ';'
 ;


rip_mode: 
    MULTICAST { $$=IM_MULTICAST; }
  | BROADCAST { $$=IM_BROADCAST; }
  | QUIET     { $$=IM_QUIET; }
  | DEFAULT   { $$=IM_DEFAULT; }
 ;

rip_iface_item:
 | METRIC expr { 
   struct rip_patt *k = rip_get_iface();
   k->metric = $2;
 }
 | MODE rip_mode {
   struct rip_patt *k = rip_get_iface();
   k->mode = $2;
 }
 ;

rip_iface_opts: 
   '{'
 | rip_iface_opts rip_iface_item ';'
 ;

rip_iface_empty: /* EMPTY */ | rip_iface_opts '}' ;

rip_iface_list:
   INTERFACE TEXT rip_iface_empty { rip_dev_add_iface($2); }
 | dev_iface_list ',' TEXT rip_iface_empty { rip_dev_add_iface($3); }
 ;

CF_CODE

void
rip_dev_add_iface(char *n)
{
  struct rip_patt *k = cfg_alloc(sizeof(struct rip_patt));

  k->i.pattern = cfg_strdup(n);
  add_tail(&RIP_CFG->iface_list, &k->i.n);
}

struct rip_patt *
rip_get_iface(void)
{
  struct rip_patt *k = TAIL(RIP_CFG->iface_list);
  if (!k)
    cf_error( "This cannot happen" );
  return k;
}


CF_END
