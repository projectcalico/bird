/*
 *	BIRD -- RIP Configuration
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

/*
To add:

version1 switch

*/


CF_HDR

#include "proto/rip/rip.h"
#include "nest/iface.h"

void rip_dev_add_iface(char *);
struct rip_patt *rip_get_iface(void);

#define RIP_CFG ((struct rip_proto_config *) this_proto)

CF_DECLS

CF_KEYWORDS(RIP, INFINITY, METRIC, PORT, PERIOD, GARBAGETIME, PASSWORDS,
	    MODE, BROADCAST, QUIET, NOLISTEN, VERSION1, 
	    AUTHENTICATION, NONE, PLAINTEXT, MD5)

%type <i> rip_mode rip_auth

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
 | RIP_CFG AUTHENTICATION rip_auth ';' {RIP_CFG->authtype = $3; }
 | RIP_CFG PASSWORDS '{' password_list '}' {RIP_CFG->passwords = $4; }
 | RIP_CFG rip_iface_list ';'
 ;

rip_auth:
   PLAINTEXT { $$=AT_PLAINTEXT; }
 | MD5 { $$=AT_MD5; }
 | NONE { $$=AT_NONE; }
 ;

/* FIXME FIXME this corrupts memory */
rip_mode: 
    BROADCAST { $$=IM_BROADCAST; }
  | QUIET     { $$=IM_QUIET; }
  | NOLISTEN  { $$=IM_NOLISTEN; }
  | VERSION1  { $$=IM_VERSION1 | IM_BROADCAST; }
 ;

rip_iface_item:
 | METRIC expr { 
   struct rip_patt *k = rip_get_iface();
   k->metric = $2;
 }
 | MODE rip_mode {
   struct rip_patt *k = rip_get_iface();
   k->mode |= $2;
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
  struct rip_patt *k = cfg_allocz(sizeof(struct rip_patt));
  k->metric = 1;
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
