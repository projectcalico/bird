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

/*
 *	Global resources
 */

void
krt_io_init(void)
{
  krt_if_io_init();
}

/*
 *	Interfaces
 */

struct proto_config *cf_kif;

static struct kif_proto *kif_proto;
static timer *kif_scan_timer;
static bird_clock_t kif_last_shot;

static void
kif_scan(timer *t)
{
  struct kif_proto *p = t->data;

  DBG("KIF: It's interface scan time...\n");
  kif_last_shot = now;
  krt_if_scan(p);
}

static void
kif_force_scan(void)
{
  if (kif_proto && kif_last_shot + 2 < now)
    {
      kif_scan(kif_scan_timer);
      tm_start(kif_scan_timer, ((struct kif_config *) kif_proto->p.cf)->scan_time);
    }
}

static struct proto *
kif_init(struct proto_config *c)
{
  struct kif_proto *p = proto_new(c, sizeof(struct kif_proto));
  return &p->p;
}

static int
kif_start(struct proto *P)
{
  struct kif_proto *p = (struct kif_proto *) P;

  kif_proto = p;
  krt_if_start(p);

  /* Start periodic interface scanning */
  kif_scan_timer = tm_new(P->pool);
  kif_scan_timer->hook = kif_scan;
  kif_scan_timer->data = p;
  kif_scan_timer->recurrent = KIF_CF->scan_time;
  kif_scan(kif_scan_timer);
  tm_start(kif_scan_timer, KIF_CF->scan_time);

  return PS_UP;
}

static int
kif_shutdown(struct proto *P)
{
  struct kif_proto *p = (struct kif_proto *) P;

  tm_stop(kif_scan_timer);
  krt_if_shutdown(p);
  kif_proto = NULL;

  if_start_update();	/* Remove all interfaces */
  if_end_update();

  return PS_DOWN;
}

struct protocol proto_unix_iface = {
  name:		"Device",
  priority:	100,
  init:		kif_init,
  start:	kif_start,
  shutdown:	kif_shutdown,
};

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

  if (old = net->routes)
    {
      if (!krt_capable(old))
	verdict = krt_capable(e) ? KRF_DELETE : KRF_SEEN;
      else if (krt_uptodate(e, net->routes))
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
	      else if (krt_capable(new))
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
	   * use multiple kernel tables anyway.
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

  kif_force_scan();
  DBG("KRT: It's route scan time...\n");
  krt_scan_fire(p);
  krt_prune(p);
}

/*
 *	Protocol glue
 */

struct proto_config *cf_krt;

static int
krt_start(struct proto *P)
{
  struct krt_proto *p = (struct krt_proto *) P;

  krt_scan_start(p);
  krt_set_start(p);

  /* Start periodic routing table scanning */
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

  tm_stop(krt_scan_timer);

  if (!KRT_CF->persist)
    krt_flush_routes(p);

  krt_set_shutdown(p);
  krt_scan_shutdown(p);

  return PS_DOWN;
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
  priority:	80,
  init:		krt_init,
  start:	krt_start,
  shutdown:	krt_shutdown,
};
