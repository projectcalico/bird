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

#define THIS_PROTO ((struct rip_proto *) this_proto)

CF_DECLS

CF_KEYWORDS(RIP, INFINITY, METRIC, PORT, PERIOD, GARBAGETIME, MODE, MULTICAST, BROADCAST, QUIET, DEFAULT)

%type <i> rip_mode

CF_GRAMMAR

CF_ADDTO(proto, rip_proto '}')

rip_proto_start: proto_start RIP {
     this_proto = proto_new(&proto_rip, sizeof(struct rip_proto));
     rip_init_instance(this_proto);
   }
 ;

rip_proto:
   rip_proto_start proto_name '{'
 | rip_proto proto_item ';'
 | rip_proto INFINITY expr ';'	{ THIS_PROTO->infinity = $3; }
 | rip_proto PORT expr ';'	{ THIS_PROTO->port = $3; }
 | rip_proto PERIOD expr ';'	{ THIS_PROTO->period = $3; }
 | rip_proto GARBAGETIME expr ';' { THIS_PROTO->garbage_time = $3; }
 | rip_proto rip_iface_list ';'
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
  add_tail(&THIS_PROTO->iface_list, &k->i.n);
}

struct rip_patt *
rip_get_iface(void)
{
  struct rip_patt *k = TAIL(THIS_PROTO->iface_list);
  if (!k)
    cf_error( "This cannot happen" );
  return k;
}


CF_END
