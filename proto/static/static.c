/*
 *	BIRD -- Static Route Generator
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include <string.h>

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/protocol.h"
#include "nest/route.h"
#include "conf/conf.h"

#include "static.h"

#define GET_DATA struct static_proto *p = (struct static_proto *) P

static void
static_install(struct static_proto *p, struct static_route *r, struct iface *ifa)
{
  net *n;
  rta a, *aa;
  rte *e;

  DBG("Installing static route %I/%d, rtd=%d\n", r->net, r->masklen, r->dest);
  bzero(&a, sizeof(a));
  a.proto = &p->p;
  a.source = (r->dest == RTD_DEVICE) ? RTS_STATIC_DEVICE : RTS_STATIC;
  a.scope = SCOPE_UNIVERSE;
  a.cast = RTC_UNICAST;
  a.dest = r->dest;
  a.tos = 0;
  a.gw = r->via;
  a.iface = ifa;
  aa = rta_lookup(&a);

  n = net_get(&master_table, a.tos, r->net, r->masklen);
  e = rte_get_temp(aa);
  e->net = n;
  e->pflags = 0;
  rte_update(n, &p->p, e);
}

static void
static_remove(struct static_proto *p, struct static_route *r)
{
  net *n;

  DBG("Removing static route %I/%d\n", r->net, r->masklen);
  n = net_find(&master_table, 0, r->net, r->masklen);
  if (n)
    rte_update(n, &p->p, NULL);
}

static void
static_start(struct proto *P)
{
  GET_DATA;
  struct static_route *r;

  DBG("Static: take off!\n");
  WALK_LIST(r, p->other_routes)
    if (r->dest == RTD_ROUTER)
      {
	struct neighbor *n = neigh_find(P, &r->via, NEF_STICKY);
	if (n)
	  {
	    n->data = r;
	    r->neigh = n;
	    static_install(p, r, n->iface);
	  }
	else
	  log(L_ERR "Static route destination %I is invalid. Ignoring.\n", r->via);
      }
    else
      static_install(p, r, NULL);
}

static void
static_neigh_notify(struct neighbor *n)
{
  DBG("Static: neighbor notify for %I: iface %p\n", n->addr, n->iface);
  if (n->iface)
    static_install((struct static_proto *) n->proto, n->data, n->iface);
  else
    static_remove((struct static_proto *) n->proto, n->data);
}

static void
static_dump_rt(struct static_route *r)
{
  debug("%16I/%2d: ", r->net, r->masklen);
  switch (r->dest)
    {
    case RTD_ROUTER:
      debug("via %I\n", r->via);
      break;
    case RTD_DEVICE:
      debug("dev %s\n", r->if_name);
      break;
    default:
      debug("rtd %d\n", r->dest);
      break;
    }
}

static void
static_dump(struct proto *P)
{
  GET_DATA;
  struct static_route *r;

  debug("Independent static routes:\n");
  WALK_LIST(r, p->other_routes)
    static_dump_rt(r);
  debug("Device static routes:\n");
  WALK_LIST(r, p->iface_routes)
    static_dump_rt(r);
}

void
static_init_instance(struct static_proto *P)
{
  struct proto *p = &P->p;

  p->preference = DEF_PREF_STATIC;
  p->start = static_start;
  p->neigh_notify = static_neigh_notify;
  p->dump = static_dump;
  init_list(&P->iface_routes);
  init_list(&P->other_routes);
}

static void
static_init(struct protocol *p)
{
}

static void
static_preconfig(struct protocol *x)
{
}

static void
static_postconfig(struct protocol *p)
{
}

struct protocol proto_static = {
  { NULL, NULL },
  "Static",
  0,
  static_init,
  static_preconfig,
  static_postconfig
};
