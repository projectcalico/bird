/*
 *	BIRD -- Static Route Generator
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
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

static void
static_install(struct proto *p, struct static_route *r, struct iface *ifa)
{
  net *n;
  rta a, *aa;
  rte *e;

  DBG("Installing static route %I/%d, rtd=%d\n", r->net, r->masklen, r->dest);
  bzero(&a, sizeof(a));
  a.proto = p;
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
  rte_update(n, p, e);
}

static void
static_remove(struct proto *p, struct static_route *r)
{
  net *n;

  DBG("Removing static route %I/%d\n", r->net, r->masklen);
  n = net_find(&master_table, 0, r->net, r->masklen);
  if (n)
    rte_update(n, p, NULL);
}

static int
static_start(struct proto *p)
{
  struct static_config *c = (void *) p->cf;
  struct static_route *r;

  DBG("Static: take off!\n");
  WALK_LIST(r, c->other_routes)
    switch (r->dest)
      {
      case RTD_ROUTER:
	{
	  struct neighbor *n = neigh_find(p, &r->via, NEF_STICKY);
	  if (n)
	    {
	      r->chain = n->data;
	      n->data = r;
	      r->neigh = n;
	      static_install(p, r, n->iface);
	    }
	  else
	    log(L_ERR "Static route destination %I is invalid. Ignoring.\n", r->via);
	  break;
	}
      case RTD_DEVICE:
	break;
      default:
	static_install(p, r, NULL);
      }
  return PS_UP;
}

static void
static_neigh_notify(struct neighbor *n)
{
  struct proto *p = n->proto;
  struct static_route *r;

  DBG("Static: neighbor notify for %I: iface %p\n", n->addr, n->iface);
  for(r=n->data; r; r=r->chain)
    if (n->iface)
      static_install(p, r, n->iface);
    else
      static_remove(p, r);
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
static_dump(struct proto *p)
{
  struct static_config *c = (void *) p->cf;
  struct static_route *r;

  debug("Independent static routes:\n");
  WALK_LIST(r, c->other_routes)
    static_dump_rt(r);
  debug("Device static routes:\n");
  WALK_LIST(r, c->iface_routes)
    static_dump_rt(r);
}

static void
static_if_notify(struct proto *p, unsigned flags, struct iface *new, struct iface *old)
{
  struct static_route *r;
  struct static_config *c = (void *) p->cf;

  if (flags & IF_CHANGE_UP)
    {
      WALK_LIST(r, c->iface_routes)
	if (!strcmp(r->if_name, new->name))
	  static_install(p, r, new);
    }
  else if (flags & IF_CHANGE_DOWN)
    {
      WALK_LIST(r, c->iface_routes)
	if (!strcmp(r->if_name, old->name))
	  static_remove(p, r);
    }
}

void
static_init_config(struct static_config *c)
{
  c->c.preference = DEF_PREF_STATIC;
  init_list(&c->iface_routes);
  init_list(&c->other_routes);
}

static struct proto *
static_init(struct proto_config *c)
{
  struct proto *p = proto_new(c, sizeof(struct proto));

  p->neigh_notify = static_neigh_notify;
  p->if_notify = static_if_notify;
  return p;
}

struct protocol proto_static = {
  name:		"Static",
  init:		static_init,
  dump:		static_dump,
  start:	static_start,
};
