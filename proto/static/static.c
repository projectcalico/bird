/*
 *	BIRD -- Static Route Generator
 *
 *	(c) 1998--2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include <string.h>

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/protocol.h"
#include "nest/route.h"
#include "nest/cli.h"
#include "conf/conf.h"
#include "lib/string.h"

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
  a.gw = r->via;
  a.iface = ifa;
  aa = rta_lookup(&a);

  n = net_get(p->table, r->net, r->masklen);
  e = rte_get_temp(aa);
  e->net = n;
  e->pflags = 0;
  rte_update(p->table, n, p, e);
  r->installed = 1;
}

static void
static_remove(struct proto *p, struct static_route *r)
{
  net *n;

  DBG("Removing static route %I/%d\n", r->net, r->masklen);
  n = net_find(p->table, r->net, r->masklen);
  if (n)
    rte_update(p->table, n, p, NULL);
  r->installed = 0;
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
	      if (n->iface)
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
static_if_notify(struct proto *p, unsigned flags, struct iface *i)
{
  struct static_route *r;
  struct static_config *c = (void *) p->cf;

  if (flags & IF_CHANGE_UP)
    {
      WALK_LIST(r, c->iface_routes)
	if (!strcmp(r->if_name, i->name))
	  static_install(p, r, i);
    }
  else if (flags & IF_CHANGE_DOWN)
    {
      WALK_LIST(r, c->iface_routes)
	if (!strcmp(r->if_name, i->name))
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

static int
static_reconfigure(struct proto *p, struct proto_config *new)
{
  return 0;
}

struct protocol proto_static = {
  name:		"Static",
  template:	"static%d",
  init:		static_init,
  dump:		static_dump,
  start:	static_start,
  reconfigure:	static_reconfigure,
};

static void
static_show_rt(struct static_route *r)
{
  byte via[STD_ADDRESS_P_LENGTH + 16];

  switch (r->dest)
    {
    case RTD_ROUTER:	bsprintf(via, "via %I", r->via); break;
    case RTD_DEVICE:	bsprintf(via, "to %s", r->if_name); break;
    case RTD_BLACKHOLE:	bsprintf(via, "blackhole"); break;
    case RTD_UNREACHABLE:	bsprintf(via, "unreachable"); break;
    case RTD_PROHIBIT:	bsprintf(via, "prohibited"); break;
    default:		bsprintf(via, "???");
    }
  cli_msg(-1009, "%I/%d %s%s", r->net, r->masklen, via, r->installed ? "" : " (dormant)");
}

void
static_show(struct proto *P)
{
  struct static_config *c = (void *) P->cf;
  struct static_route *r;

  WALK_LIST(r, c->other_routes)
    static_show_rt(r);
  WALK_LIST(r, c->iface_routes)
    static_show_rt(r);
  cli_msg(0, "");
}
