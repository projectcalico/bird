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

static int krt_uptodate(rte *k, rte *e);

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
 *	Inherited Routes
 */

#ifdef KRT_ALLOW_LEARN

static inline int
krt_same_key(rte *a, rte *b)
{
  return a->u.krt.proto == b->u.krt.proto &&
         a->u.krt.metric == b->u.krt.metric &&
         a->u.krt.type == b->u.krt.type;
}

static void
krt_learn_announce_update(struct krt_proto *p, rte *e)
{
  net *n = e->net;
  rta *aa = rta_clone(e->attrs);
  rte *ee = rte_get_temp(aa);
  net *nn = net_get(p->p.table, 0, n->n.prefix, n->n.pxlen);		/* FIXME: TOS */
  ee->net = nn;
  ee->pflags = 0;
  ee->u.krt = e->u.krt;
  rte_update(nn, &p->p, ee);
}

static void
krt_learn_announce_delete(struct krt_proto *p, net *n)
{
  n = net_find(p->p.table, 0, n->n.prefix, n->n.pxlen);	      		/* FIXME: TOS */
  if (n)
    rte_update(n, &p->p, NULL);
}

static void
krt_learn_scan(struct krt_proto *p, rte *e)
{
  net *n0 = e->net;
  net *n = net_get(&p->krt_table, 0, n0->n.prefix, n0->n.pxlen);	/* FIXME: TOS */
  rte *m, **mm;

  e->attrs->source = RTS_INHERIT;

  for(mm=&n->routes; m = *mm; mm=&m->next)
    if (krt_same_key(m, e))
      break;
  if (m)
    {
      if (krt_uptodate(m, e))
	{
	  DBG("krt_learn_scan: SEEN\n");
	  rte_free(e);
	  m->u.krt.seen = 1;
	}
      else
	{
	  DBG("krt_learn_scan: OVERRIDE\n");
	  *mm = m->next;
	  rte_free(m);
	  m = NULL;
	}
    }
  else
    DBG("krt_learn_scan: CREATE\n");
  if (!m)
    {
      e->attrs = rta_lookup(e->attrs);
      e->next = n->routes;
      n->routes = e;
      e->u.krt.seen = 1;
    }
}

/* FIXME: Add dump function */

static void
krt_learn_prune(struct krt_proto *p)
{
  struct fib *fib = &p->krt_table.fib;
  struct fib_iterator fit;

  DBG("Pruning inheritance data...\n");

  FIB_ITERATE_INIT(&fit, fib);
again:
  FIB_ITERATE_START(fib, &fit, f)
    {
      net *n = (net *) f;
      rte *e, **ee, *best, **pbest, *old_best;

      old_best = n->routes;
      best = NULL;
      pbest = NULL;
      ee = &n->routes;
      while (e = *ee)
	{
	  if (!e->u.krt.seen)
	    {
	      *ee = e->next;
	      rte_free(e);
	      continue;
	    }
	  if (!best || best->u.krt.metric > e->u.krt.metric)
	    {
	      best = e;
	      pbest = ee;
	    }
	  e->u.krt.seen = 0;
	  ee = &e->next;
	}
      if (!n->routes)
	{
	  DBG("%I/%d: deleting\n", n->n.prefix, n->n.pxlen);
	  if (old_best)
	    {
	      krt_learn_announce_delete(p, n);
	      n->n.flags &= ~KRF_INSTALLED;
	    }
	  FIB_ITERATE_PUT(&fit, f);
	  fib_delete(fib, f);
	  goto again;
	}
      *pbest = best->next;
      best->next = n->routes;
      n->routes = best;
      if (best != old_best || !(n->n.flags & KRF_INSTALLED))
	{
	  DBG("%I/%d: announcing (metric=%d)\n", n->n.prefix, n->n.pxlen, best->u.krt.metric);
	  krt_learn_announce_update(p, best);
	  n->n.flags |= KRF_INSTALLED;
	}
      else
	DBG("%I/%d: uptodate (metric=%d)\n", n->n.prefix, n->n.pxlen, best->u.krt.metric);
    }
  FIB_ITERATE_END(f);
}

static void
krt_learn_async(struct krt_proto *p, rte *e, int new)
{
  net *n0 = e->net;
  net *n = net_get(&p->krt_table, 0, n0->n.prefix, n0->n.pxlen);	/* FIXME: TOS */
  rte *g, **gg, *best, **bestp, *old_best;

  e->attrs->source = RTS_INHERIT;

  old_best = n->routes;
  for(gg=&n->routes; g = *gg; gg = &g->next)
    if (krt_same_key(g, e))
      break;
  if (new)
    {
      if (g)
	{
	  if (krt_uptodate(g, e))
	    {
	      DBG("krt_learn_async: same\n");
	      rte_free(e);
	      return;
	    }
	  DBG("krt_learn_async: update\n");
	  *gg = g->next;
	  rte_free(g);
	}
      else
	DBG("krt_learn_async: create\n");
      e->attrs = rta_lookup(e->attrs);
      e->next = n->routes;
      n->routes = e;
    }
  else if (!g)
    {
      DBG("krt_learn_async: not found\n");
      rte_free(e);
      return;
    }
  else
    {
      DBG("krt_learn_async: delete\n");
      *gg = g->next;
      rte_free(e);
      rte_free(g);
    }
  best = n->routes;
  bestp = &n->routes;
  for(gg=&n->routes; g=*gg; gg=&g->next)
    if (best->u.krt.metric > g->u.krt.metric)
      {
	best = g;
	bestp = gg;
      }
  if (best)
    {
      *bestp = best->next;
      best->next = n->routes;
      n->routes = best;
    }
  if (best != old_best)
    {
      DBG("krt_learn_async: distributing change\n");
      if (best)
	{
	  krt_learn_announce_update(p, best);
	  n->n.flags |= KRF_INSTALLED;
	}
      else
	{
	  n->routes = NULL;
	  krt_learn_announce_delete(p, n);
	  n->n.flags &= ~KRF_INSTALLED;
	}
    }
}

static void
krt_learn_init(struct krt_proto *p)
{
  if (KRT_CF->learn)
    rt_setup(p->p.pool, &p->krt_table, "Inherited");
}

static void
krt_dump(struct proto *P)
{
  struct krt_proto *p = (struct krt_proto *) P;

  if (!KRT_CF->learn)
    return;
  debug("KRT: Table of inheritable routes\n");
  rt_dump(&p->krt_table);
}

static void
krt_dump_attrs(rte *e)
{
  debug(" [m=%d,p=%d,t=%d]", e->u.krt.metric, e->u.krt.proto, e->u.krt.type);
}

#endif

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
	    krt_set_notify(p, e->net, NULL, e);
	}
    }
  FIB_WALK_END;
}

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
  int src = e->u.krt.src;
  int verdict;

#ifdef CONFIG_AUTO_ROUTES
  if (e->attrs->dest == RTD_DEVICE)
    {
      /* It's a device route. Probably a kernel-generated one. */
      verdict = KRF_IGNORE;
      goto sentenced;
    }
#endif

#ifdef KRT_ALLOW_LEARN
  if (src == KRT_SRC_ALIEN)
    {
      if (KRT_CF->learn)
	krt_learn_scan(p, e);
      else
	DBG("krt_parse_entry: Alien route, ignoring\n");
      return;
    }
#endif

  if (net->n.flags & KRF_VERDICT_MASK)
    {
      /* Route to this destination was already seen. Strange, but it happens... */
      DBG("Already seen.\n");
      return;
    }

  if (net->n.flags & KRF_INSTALLED)
    {
      old = net->routes;
      ASSERT(old);
      if (krt_uptodate(e, old))
	verdict = KRF_SEEN;
      else
	verdict = KRF_UPDATE;
    }
  else
    verdict = KRF_DELETE;

sentenced:
  DBG("krt_parse_entry: verdict=%s\n", ((char *[]) { "CREATE", "SEEN", "UPDATE", "DELETE", "IGNORE" }) [verdict]);

  net->n.flags = (net->n.flags & ~KRF_VERDICT_MASK) | verdict;
  if (verdict == KRF_UPDATE || verdict == KRF_DELETE)
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
      int verdict = f->flags & KRF_VERDICT_MASK;
      rte *new, *old;

      if (verdict != KRF_CREATE && verdict != KRF_SEEN && verdict != KRF_IGNORE)
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
	  if (new && (f->flags & KRF_INSTALLED))
	    {
	      DBG("krt_prune: reinstalling %I/%d\n", n->n.prefix, n->n.pxlen);
	      krt_set_notify(p, n, new, NULL);
	    }
	  break;
	case KRF_SEEN:
	case KRF_IGNORE:
	  /* Nothing happens */
	  break;
	case KRF_UPDATE:
	  DBG("krt_prune: updating %I/%d\n", n->n.prefix, n->n.pxlen);
	  krt_set_notify(p, n, new, old);
	  break;
	case KRF_DELETE:
	  DBG("krt_prune: deleting %I/%d\n", n->n.prefix, n->n.pxlen);
	  krt_set_notify(p, n, NULL, old);
	  break;
	default:
	  bug("krt_prune: invalid route status");
	}
      if (old)
	rte_free(old);
      f->flags &= ~KRF_VERDICT_MASK;
    }
  FIB_WALK_END;

#ifdef KRT_ALLOW_LEARN
  if (KRT_CF->learn)
    krt_learn_prune(p);
#endif
}

void
krt_got_route_async(struct krt_proto *p, rte *e, int new)
{
  net *net = e->net;
  rte *old = net->routes;
  int src = e->u.krt.src;

  switch (src)
    {
    case KRT_SRC_BIRD:
      ASSERT(0);
    case KRT_SRC_REDIRECT:
      DBG("It's a redirect, kill him! Kill! Kill!\n");
      krt_set_notify(p, net, NULL, e);
      break;
    case KRT_SRC_ALIEN:
#ifdef KRT_ALLOW_LEARN
      if (KRT_CF->learn)
	{
	  krt_learn_async(p, e, new);
	  return;
	}
#endif
      /* Fall-thru */
    default:
      DBG("Discarding\n");
      rte_update(net, &p->p, NULL);
    }
  rte_free(e);
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
 *	Updates
 */

static void
krt_notify(struct proto *P, net *net, rte *new, rte *old)
{
  struct krt_proto *p = (struct krt_proto *) P;

  if (new && (!krt_capable(new) || new->attrs->source == RTS_INHERIT))
    new = NULL;
  if (!(net->n.flags & KRF_INSTALLED))
    old = NULL;
  if (new)
    net->n.flags |= KRF_INSTALLED;
  else
    net->n.flags &= ~KRF_INSTALLED;
  krt_set_notify(p, net, new, old);
}

/*
 *	Protocol glue
 */

struct proto_config *cf_krt;

static int
krt_start(struct proto *P)
{
  struct krt_proto *p = (struct krt_proto *) P;

#ifdef KRT_ALLOW_LEARN
  krt_learn_init(p);
#endif

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

  p->p.rt_notify = krt_notify;
  return &p->p;
}

struct protocol proto_unix_kernel = {
  name:		"Kernel",
  priority:	80,
  init:		krt_init,
  start:	krt_start,
  shutdown:	krt_shutdown,
#ifdef KRT_ALLOW_LEARN
  dump:		krt_dump,
  dump_attrs:	krt_dump_attrs,
#endif
};
