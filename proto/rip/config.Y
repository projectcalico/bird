/*
 *	BIRD -- RIP Configuration
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

#include "proto/rip/rip.h"
#include "nest/iface.h"

void rip_dev_add_iface(char *);
struct iface_patt *rip_get_iface(void);

#define THIS_PROTO ((struct rip_proto *) this_proto)

CF_DECLS

CF_KEYWORDS(RIP, INFINITY, METRIC, PORT, PERIOD, GARBAGETIME)

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

rip_iface_item:
 | METRIC expr { 
   struct iface_patt *k = rip_get_iface();
   k->u.rip.metric = $2;
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
  struct iface_patt *k = cfg_alloc(sizeof(struct iface_patt));

  k->pattern = cfg_strcpy(n);
  add_tail(&THIS_PROTO->iface_list, &k->n);
}

struct iface_patt *
rip_get_iface(void)
{
  struct iface_patt *k = TAIL(THIS_PROTO->iface_list);
  if (!k)
    cf_error( "This cannot happen" );
  return k;
}


CF_END
