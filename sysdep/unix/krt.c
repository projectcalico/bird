/*
 *	BIRD -- UNIX Kernel Synchronization
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/route.h"
#include "nest/protocol.h"
#include "lib/timer.h"

#include "unix.h"
#include "krt.h"

struct proto_config *cf_krt;

/*
 *	Routes
 */

static void
krt_flush_routes(struct krt_proto *p)
{
  struct rtable *t = &master_table;

  DBG("Flushing kernel routes...\n");
  while (t && t->tos)
    t = t->sibling;
  if (!t)
    return;
  FIB_WALK(&t->fib, f)
    {
      net *n = (net *) f;
      rte *e = n->routes;
      if (e)
	{
	  rta *a = e->attrs;
	  if (a->source != RTS_DEVICE && a->source != RTS_INHERIT)
	    krt_set_notify(&p->p, e->net, NULL, e);
	}
    }
  FIB_WALK_END;
}

/* FIXME: Inbound/outbound route filtering? */
/* FIXME: Synchronization of multiple routing tables? */

static int
krt_uptodate(rte *k, rte *e)
{
  rta *ka = k->attrs, *ea = e->attrs;

  if (ka->dest != ea->dest)
    return 0;
  switch (ka->dest)
    {
    case RTD_ROUTER:
      return ipa_equal(ka->gw, ea->gw);
    case RTD_DEVICE:
      return !strcmp(ka->iface->name, ea->iface->name);
    default:
      return 1;
    }
}

/*
 *  This gets called back when the low-level scanning code discovers a route.
 *  We expect that the route is a temporary rte and its attributes are uncached.
 */

void
krt_got_route(struct krt_proto *p, rte *e)
{
  rte *old;
  net *net = e->net;
  int src = e->u.krt_sync.src;
  int verdict;

  if (net->n.flags)
    {
      /* Route to this destination was already seen. Strange, but it happens... */
      DBG("Already seen.\n");
      return;
    }

  old = net->routes;
  if (old && !krt_capable(old))
    old = NULL;
  if (old)
    {
      if (krt_uptodate(e, net->routes))
	verdict = KRF_SEEN;
      else
	verdict = KRF_UPDATE;
    }
  else if (KRT_CF->learn && !net->routes && (src == KRT_SRC_ALIEN || src < 0))
    verdict = KRF_LEARN;
  else
    verdict = KRF_DELETE;

  DBG("krt_parse_entry: verdict=%s\n", ((char *[]) { "CREATE", "SEEN", "UPDATE", "DELETE", "LEARN" }) [verdict]);

  net->n.flags = verdict;
  if (verdict != KRF_SEEN)
    {
      /* Get a cached copy of attributes and link the route */
      rta *a = e->attrs;
      a->source = RTS_DUMMY;
      e->attrs = rta_lookup(a);
      e->next = net->routes;
      net->routes = e;
    }
  else
    rte_free(e);
}

static void
krt_prune(struct krt_proto *p)
{
  struct proto *pp = &p->p;
  struct rtable *t = &master_table;
  struct fib_node *f;

  DBG("Pruning routes...\n");
  while (t && t->tos)
    t = t->sibling;
  if (!t)
    return;
  FIB_WALK(&t->fib, f)
    {
      net *n = (net *) f;
      int verdict = f->flags;
      rte *new, *old;

      if (verdict != KRF_CREATE && verdict != KRF_SEEN)
	{
	  old = n->routes;
	  n->routes = old->next;
	}
      else
	old = NULL;
      new = n->routes;

      switch (verdict)
	{
	case KRF_CREATE:
	  if (new)
	    {
	      if (new->attrs->source == RTS_INHERIT)
		{
		  DBG("krt_prune: removing inherited %I/%d\n", n->n.prefix, n->n.pxlen);
		  rte_update(n, pp, NULL);
		}
	      else
		{
		  DBG("krt_prune: reinstalling %I/%d\n", n->n.prefix, n->n.pxlen);
		  krt_set_notify(pp, n, new, NULL);
		}
	    }
	  break;
	case KRF_SEEN:
	  /* Nothing happens */
	  break;
	case KRF_UPDATE:
	  DBG("krt_prune: updating %I/%d\n", n->n.prefix, n->n.pxlen);
	  krt_set_notify(pp, n, new, old);
	  break;
	case KRF_DELETE:
	  DBG("krt_prune: deleting %I/%d\n", n->n.prefix, n->n.pxlen);
	  krt_set_notify(pp, n, NULL, old);
	  break;
	case KRF_LEARN:
	  DBG("krt_prune: learning %I/%d\n", n->n.prefix, n->n.pxlen);
	  rte_update(n, pp, new);
	  break;
	default:
	  bug("krt_prune: invalid route status");
	}

      if (old)
	rte_free(old);
      f->flags = 0;
    }
  FIB_WALK_END;
}

void
krt_got_route_async(struct krt_proto *p, rte *e, int new)
{
  net *net = e->net;
  rte *old = net->routes;
  int src = e->u.krt_sync.src;

  switch (src)
    {
    case KRT_SRC_BIRD:
      ASSERT(0);
    case KRT_SRC_REDIRECT:
      DBG("It's a redirect, kill him! Kill! Kill!\n");
      krt_set_notify(&p->p, net, NULL, e);
      break;
    default:	/* Alien or unspecified */
      if (KRT_CF->learn && new)
	{
	  /*
	   * FIXME: This is limited to one inherited route per destination as we
	   * use single protocol for all inherited routes. Probably leave it
	   * as-is (and document it :)), because the correct solution is to
	   * multiple kernel tables anyway.
	   */
	  DBG("Learning\n");
	  rte_update(net, &p->p, e);
	}
      else
	{
	  DBG("Discarding\n");
	  rte_update(net, &p->p, NULL);
	}
    }
}

/*
 *	Periodic scanning
 */

static timer *krt_scan_timer;

static void
krt_scan(timer *t)
{
  struct krt_proto *p = t->data;

  DBG("KRT: It's scan time...\n");
  krt_if_scan(p);

  p->accum_time += KRT_CF->scan_time;
  if (KRT_CF->route_scan_time && p->accum_time >= KRT_CF->route_scan_time)
    {
      p->accum_time %= KRT_CF->route_scan_time;
      DBG("Scanning kernel routing table...\n");
      krt_scan_fire(p);
      krt_prune(p);
    }
}

/*
 *	Protocol glue
 */

static int
krt_start(struct proto *P)
{
  struct krt_proto *p = (struct krt_proto *) P;

  p->accum_time = KRT_CF->route_scan_time - KRT_CF->scan_time;

  krt_if_start(p);
  krt_scan_start(p);
  krt_set_start(p);

  /* Start periodic interface scanning */
  krt_scan_timer = tm_new(P->pool);
  krt_scan_timer->hook = krt_scan;
  krt_scan_timer->data = p;
  krt_scan_timer->recurrent = KRT_CF->scan_time;
  krt_scan(krt_scan_timer);
  tm_start(krt_scan_timer, KRT_CF->scan_time);

  return PS_UP;
}

int
krt_shutdown(struct proto *P)
{
  struct krt_proto *p = (struct krt_proto *) P;

  if (!KRT_CF->persist)
    krt_flush_routes(p);

  krt_set_shutdown(p);
  krt_scan_shutdown(p);

  /* Stop periodic interface scans */
  tm_stop(krt_scan_timer);
  krt_if_shutdown(p);
  /* FIXME: What should we do with interfaces? */

  return PS_DOWN;
}

static void
krt_preconfig(struct protocol *x, struct config *c)
{
  struct krt_config *z = proto_config_new(&proto_unix_kernel, sizeof(struct krt_config));

  cf_krt = &z->c;
  z->c.preference = DEF_PREF_UKR;
  z->scan_time = z->route_scan_time = 60;
  z->learn = z->persist = 0;

  krt_scan_preconfig(z);
  krt_set_preconfig(z);
  krt_if_preconfig(z);
}

static struct proto *
krt_init(struct proto_config *c)
{
  struct krt_proto *p = proto_new(c, sizeof(struct krt_proto));

  p->p.rt_notify = krt_set_notify;
  return &p->p;
}

struct protocol proto_unix_kernel = {
  name:		"Kernel",
  priority:	90,
  preconfig:	krt_preconfig,
  init:		krt_init,
  start:	krt_start,
  shutdown:	krt_shutdown,
};
