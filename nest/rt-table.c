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

  n->next = NULL;
  n->routes = NULL;
}

void
rt_setup(rtable *t, char *name)
{
  bzero(t, sizeof(*t));
  fib_init(&t->fib, &root_pool, sizeof(rte), 0, rte_init);
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
  e->pref = a->proto->preference;
  e->lastmod = 0;			/* FIXME */
  return e;
}

static int				/* Actually better or at least as good as */
rte_better(rte *new, rte *old)
{
  if (!old)
    return 1;
  if (new->pref > old->pref)
    return 1;
  if (new->pref < old->pref)
    return 0;
  if (new->attrs->proto != old->attrs->proto)
    {
      /* FIXME!!! */
      die("Different protocols, but identical preferences => oops");
    }
  return new->attrs->proto->rte_better(new, old);
}

void
rte_announce(rte *new, rte *old)
{
  struct proto *p;

  WALK_LIST(p, proto_list)
    if (!new || new->attrs->proto != p)
      p->rt_notify(p, new, old);
}

static inline void
rte_free(rte *e)
{
  rta_free(e->attrs);
  sl_free(rte_slab, e);
}

void
rte_update(net *net, rte *new)
{
  rte *old_best = net->routes;
  rte *old = NULL;
  rte **k, *r, *s;
  struct proto *p = new->attrs->proto;

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

  if (rte_better(new, old_best))	/* It's a new optimal route => announce and relink it */
    {
      rte_announce(new, old_best);
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
	  rte_announce(r, old_best);
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
    rte_free(old);
}

void
rte_dump(rte *r)
{
}

void
rt_dump(rtable *t)
{
}

void
rt_dump_all(void)
{
}

void
rt_init(void)
{
  rta_init();
  rt_setup(&master_table, "master");
  rte_slab = sl_new(&root_pool, sizeof(rte));
}
