/*
 *	BIRD -- Routing Table
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <string.h>

#include "nest/bird.h"
#include "nest/route.h"
#include "nest/protocol.h"
#include "lib/resource.h"

rtable master_table;
static slab *rte_slab;

void
rte_init(struct fib_node *N)
{
  net *n = (net *) N;

  N->flags = 0;
  n->routes = NULL;
}

void
rt_setup(rtable *t, char *name)
{
  bzero(t, sizeof(*t));
  fib_init(&t->fib, &root_pool, sizeof(rte), 0, rte_init);
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
      rt_setup(t, NULL);
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
  if (new->attrs->proto != old->attrs->proto)
    {
      /* FIXME!!! */
      bug("Different protocols, but identical preferences => oops");
    }
  if (better = new->attrs->proto->rte_better)
    return better(new, old);
  return 0;
}

void
rte_announce(net *net, rte *new, rte *old)
{
  struct proto *p;

  WALK_LIST(p, proto_list)
    if (p->rt_notify)
      p->rt_notify(p, net, new, old);
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
	    p->rt_notify(p, n, e, NULL);
	}
      FIB_WALK_END;
      t = t->sibling;
    }
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

  if (new && !(new->attrs->aflags & RTAF_CACHED)) /* Need to copy attributes */
    new->attrs = rta_lookup(new->attrs);

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
  debug("PF=%02x pref=%d lm=%d ", e->pflags, e->pref, now-e->lastmod);
  rta_dump(e->attrs);
  if (e->flags & REF_CHOSEN)
    debug(" [*]");
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

void
rt_init(void)
{
  rta_init();
  rt_setup(&master_table, "master");
  rte_slab = sl_new(&root_pool, sizeof(rte));
}
