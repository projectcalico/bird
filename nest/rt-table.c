/*
 *	BIRD -- Routing Table
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <string.h>

#define LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/route.h"
#include "nest/protocol.h"
#include "lib/resource.h"
#include "lib/event.h"
#include "filter/filter.h"

rtable master_table;
static slab *rte_slab;

#define RT_GC_MIN_TIME 5		/* FIXME: Make configurable */
#define RT_GC_MIN_COUNT 100

static pool *rt_table_pool;
static event *rt_gc_event;
static bird_clock_t rt_last_gc;
static int rt_gc_counter;

static void
rte_init(struct fib_node *N)
{
  net *n = (net *) N;

  N->flags = 0;
  n->routes = NULL;
}

void
rt_setup(pool *p, rtable *t, char *name)
{
  bzero(t, sizeof(*t));
  fib_init(&t->fib, p, sizeof(rte), 0, rte_init);
  t->name = name;
}

net *
net_find(rtable *tab, unsigned tos, ip_addr mask, unsigned len)
{
  while (tab && tab->tos != tos)
    tab = tab->sibling;
  if (!tab)
    return NULL;
  return (net *) fib_find(&tab->fib, &mask, len);
}

net *
net_get(rtable *tab, unsigned tos, ip_addr mask, unsigned len)
{
  rtable *t = tab;

  while (t && t->tos != tos)
    t = t->sibling;
  if (!t)
    {
      while (tab->sibling)
	tab = tab->sibling;
      t = mb_alloc(&root_pool, sizeof(rtable));
      rt_setup(&root_pool, t, NULL);	/* FIXME: Either delete all the TOS logic or use the right pool */
      tab->sibling = t;
      t->tos = tos;
    }
  return (net *) fib_get(&t->fib, &mask, len);
}

rte *
rte_find(net *net, struct proto *p)
{
  rte *e = net->routes;

  while (e && e->attrs->proto != p)
    e = e->next;
  return e;
}

rte *
rte_get_temp(rta *a)
{
  rte *e = sl_alloc(rte_slab);

  e->attrs = a;
  e->flags = 0;
  e->pref = a->proto->preference;
  return e;
}

static int				/* Actually better or at least as good as */
rte_better(rte *new, rte *old)
{
  int (*better)(rte *, rte *);

  if (!old)
    return 1;
  if (new->pref > old->pref)
    return 1;
  if (new->pref < old->pref)
    return 0;
  if (new->attrs->proto->proto != old->attrs->proto->proto)
    bug("Different protocols, but identical preferences => oops");	/* FIXME */
  if (better = new->attrs->proto->rte_better)
    return better(new, old);
  return 0;
}

static inline void
do_rte_announce(struct proto *p, net *net, rte *new, rte *old)
{
  if (p->out_filter)
    {
      if (old && f_run(p->out_filter, old, NULL) != F_ACCEPT)
	old = NULL;
      if (new && f_run(p->out_filter, new, NULL) != F_ACCEPT)
	new = NULL;
    }
  if (new || old)
    p->rt_notify(p, net, new, old);
}

void
rte_announce(net *net, rte *new, rte *old)
{
  struct proto *p;

  WALK_LIST(p, proto_list)
    {
      ASSERT(p->core_state == FS_HAPPY);
      if (p->rt_notify)
	do_rte_announce(p, net, new, old);
    }
}

void
rt_feed_baby(struct proto *p)
{
  rtable *t = &master_table;

  if (!p->rt_notify)
    return;
  debug("Announcing routes to new protocol %s\n", p->name);
  while (t)
    {
      FIB_WALK(&t->fib, fn)
	{
	  net *n = (net *) fn;
	  rte *e;
	  for(e=n->routes; e; e=e->next)
	    do_rte_announce(p, n, e, NULL);
	}
      FIB_WALK_END;
      t = t->sibling;
    }
}

static inline int
rte_validate(rte *e)
{
  int c;
  net *n = e->net;

  ASSERT(!ipa_nonzero(ipa_and(n->n.prefix, ipa_not(ipa_mkmask(n->n.pxlen)))));
  if (n->n.pxlen)
    {
      c = ipa_classify(n->n.prefix);
      if (c < 0 || !(c & IADDR_HOST))
	{
	  if (!ipa_nonzero(n->n.prefix) && n->n.pxlen <= 1)
	    return 1;		/* Default route and half-default route is OK */
	  log(L_WARN "Ignoring bogus route %I/%d received from %I via %s",
	      n->n.prefix, n->n.pxlen, e->attrs->from, e->attrs->proto->name);
	  return 0;
	}
      if ((c & IADDR_SCOPE_MASK) == SCOPE_HOST)
	{
	  int s = e->attrs->source;
	  if (s != RTS_STATIC && s != RTS_DEVICE && s != RTS_STATIC_DEVICE)
	    {
	      log(L_WARN "Ignoring host scope route %I/%d received from %I via %s",
		  n->n.prefix, n->n.pxlen, e->attrs->from, e->attrs->proto->name);
	      return 0;
	    }
	}
    }
  return 1;
}

void
rte_free(rte *e)
{
  if (e->attrs->aflags & RTAF_CACHED)
    rta_free(e->attrs);
  sl_free(rte_slab, e);
}

static inline void
rte_free_quick(rte *e)
{
  rta_free(e->attrs);
  sl_free(rte_slab, e);
}

void
rte_update(net *net, struct proto *p, rte *new)
{
  rte *old_best = net->routes;
  rte *old = NULL;
  rte **k, *r, *s;

  if (new)
    {
      if (!rte_validate(new) || p->in_filter && f_run(p->in_filter, new, NULL) != F_ACCEPT)
	{
	  rte_free(new);
	  return;
	}
      if (!(new->attrs->aflags & RTAF_CACHED)) /* Need to copy attributes */
	new->attrs = rta_lookup(new->attrs);
    }

  k = &net->routes;			/* Find and remove original route from the same protocol */
  while (old = *k)
    {
      if (old->attrs->proto == p)
	{
	  *k = old->next;
	  break;
	}
      k = &old->next;
    }

  if (new && rte_better(new, old_best))	/* It's a new optimal route => announce and relink it */
    {
      rte_announce(net, new, old_best);
      new->next = net->routes;
      net->routes = new;
    }
  else
    {
      if (old == old_best)		/* It has _replaced_ the old optimal route */
	{
	  r = new;			/* Find new optimal route and announce it */
	  for(s=net->routes; s; s=s->next)
	    if (rte_better(s, r))
	      r = s;
	  rte_announce(net, r, old_best);
	  if (r)			/* Re-link the new optimal route */
	    {
	      k = &net->routes;
	      while (s = *k)
		{
		  if (s == r)
		    {
		      *k = r->next;
		      break;
		    }
		  k = &s->next;
		}
	      r->next = net->routes;
	      net->routes = r;
	      if (!r && rt_gc_counter++ >= RT_GC_MIN_COUNT && rt_last_gc + RT_GC_MIN_TIME <= now)
		ev_schedule(rt_gc_event);
	    }
	}
      if (new)				/* Link in the new non-optimal route */
	{
	  new->next = old_best->next;
	  old_best->next = new;
	}
    }
  if (old)
    {
      if (p->rte_remove)
	p->rte_remove(net, old);
      rte_free_quick(old);
    }
  if (new)
    {
      new->lastmod = now;
      if (p->rte_insert)
	p->rte_insert(net, new);
    }
}

void
rte_discard(rte *old)			/* Non-filtered route deletion, used during garbage collection */
{
  rte_update(old->net, old->attrs->proto, NULL);
}

void
rte_dump(rte *e)
{
  net *n = e->net;
  if (n)
    debug("%1I/%2d ", n->n.prefix, n->n.pxlen);
  else
    debug("??? ");
  debug("KF=%02x PF=%02x pref=%d lm=%d ", n->n.flags, e->pflags, e->pref, now-e->lastmod);
  rta_dump(e->attrs);
  if (e->attrs->proto->proto->dump_attrs)
    e->attrs->proto->proto->dump_attrs(e);
  debug("\n");
}

void
rt_dump(rtable *t)
{
  rte *e;
  net *n;

  debug("Dump of routing table <%s>\n", t->name);
  while (t)
    {
      debug("Routes for TOS %02x:\n", t->tos);
#ifdef DEBUGGING
      fib_check(&t->fib);
#endif
      FIB_WALK(&t->fib, fn)
	{
	  n = (net *) fn;
	  for(e=n->routes; e; e=e->next)
	    rte_dump(e);
	}
      FIB_WALK_END;
      t = t->sibling;
    }
  debug("\n");
}

void
rt_dump_all(void)
{
  rt_dump(&master_table);
}

static void
rt_gc(void *unused)
{
  DBG("Entered routing table garbage collector after %d seconds and %d deletes\n", (int)(now - rt_last_gc), rt_gc_counter);
  rt_prune(&master_table);
  rt_last_gc = now;
  rt_gc_counter = 0;
}

void
rt_init(void)
{
  rta_init();
  rt_table_pool = rp_new(&root_pool, "Routing tables");
  rt_setup(rt_table_pool, &master_table, "master");
  rte_slab = sl_new(rt_table_pool, sizeof(rte));
  rt_last_gc = now;
  rt_gc_event = ev_new(rt_table_pool);
  rt_gc_event->hook = rt_gc;
}

void
rt_prune(rtable *tab)
{
  struct fib_iterator fit;
  int rcnt = 0, rdel = 0, ncnt = 0, ndel = 0;

  DBG("Pruning route table %s\n", tab->name);
  while (tab)
    {
      FIB_ITERATE_INIT(&fit, &tab->fib);
    again:
      FIB_ITERATE_START(&tab->fib, &fit, f)
	{
	  net *n = (net *) f;
	  rte *e;
	  ncnt++;
	rescan:
	  for (e=n->routes; e; e=e->next, rcnt++)
	    if (e->attrs->proto->core_state != FS_HAPPY)
	      {
		rte_discard(e);
		rdel++;
		goto rescan;
	      }
	  if (!n->routes)		/* Orphaned FIB entry? */
	    {
	      FIB_ITERATE_PUT(&fit, f);
	      fib_delete(&tab->fib, f);
	      ndel++;
	      goto again;
	    }
	}
      FIB_ITERATE_END(f);
      tab = tab->sibling;
    }
  DBG("Pruned %d of %d routes and %d of %d networks\n", rcnt, rdel, ncnt, ndel);
}
