/*
 *	BIRD -- The Border Gateway Protocol
 *
 *	(c) 2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/protocol.h"
#include "nest/route.h"
#include "conf/conf.h"

#include "bgp.h"

static void
bgp_rt_notify(struct proto *P, net *n, rte *new, rte *old, ea_list *tmpa)
{
}

static struct proto *
bgp_init(struct proto_config *C)
{
  struct bgp_config *c = (struct bgp_config *) C;
  struct proto *P = proto_new(C, sizeof(struct bgp_proto));
  struct bgp_proto *p = (struct bgp_proto *) P;

  P->rt_notify = bgp_rt_notify;
  return P;
}

static int
bgp_start(struct proto *P)
{
  return PS_UP;
}

static int
bgp_shutdown(struct proto *P)
{
  return PS_DOWN;
}

void
bgp_check(struct bgp_config *c)
{
  if (!c->local_as)
    cf_error("Local AS number must be set");
  if (!c->remote_as)
    cf_error("Neighbor must be configured");
}

struct protocol proto_bgp = {
  name:			"BGP",
  template:		"bgp%d",
  init:			bgp_init,
  start:		bgp_start,
  shutdown:		bgp_shutdown,
#if 0
  dump:			bgp_dump,
  get_status:		bgp_get_status,
  get_route_info:	bgp_get_route_info,
  show_route_data:	bgp_show_route_data
#endif
};
