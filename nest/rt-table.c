/*
 *	BIRD -- Routing Table
 *
 *	(c) 1998--2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#undef LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/route.h"
#include "nest/protocol.h"
#include "nest/cli.h"
#include "nest/iface.h"
#include "lib/resource.h"
#include "lib/event.h"
#include "lib/string.h"
#include "conf/conf.h"
#include "filter/filter.h"
#include "lib/string.h"

static slab *rte_slab;
static linpool *rte_update_pool;

static pool *rt_table_pool;
static list routing_tables;

static void rt_format_via(rte *e, byte *via);

static void
rte_init(struct fib_node *N)
{
  net *n = (net *) N;

  N->flags = 0;
  n->routes = NULL;
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

rte *
rte_do_cow(rte *r)
{
  rte *e = sl_alloc(rte_slab);

  memcpy(e, r, sizeof(rte));
  e->attrs = rta_clone(r->attrs);
  e->flags = 0;
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
    {
      /*
       *  If the user has configured protocol preferences, so that two different protocols
       *  have the same preference, try to break the tie by comparing addresses. Not too
       *  useful, but keeps the ordering of routes unambiguous.
       */
      return new->attrs->proto->proto > old->attrs->proto->proto;
    }
  if (better = new->attrs->proto->rte_better)
    return better(new, old);
  return 0;
}

static void
rte_trace(struct proto *p, rte *e, int dir, char *msg)
{
  byte via[STD_ADDRESS_P_LENGTH+32];

  rt_format_via(e, via);
  log(L_TRACE "%s %c %s %I/%d %s", p->name, dir, msg, e->net->n.prefix, e->net->n.pxlen, via);
}

static inline void
rte_trace_in(unsigned int flag, struct proto *p, rte *e, char *msg)
{
  if (p->debug & flag)
    rte_trace(p, e, '>', msg);
}

static inline void
rte_trace_out(unsigned int flag, struct proto *p, rte *e, char *msg)
{
  if (p->debug & flag)
    rte_trace(p, e, '<', msg);
}

static inline void
do_rte_announce(struct announce_hook *a, net *net, rte *new, rte *old, ea_list *tmpa, int class)
{
  struct proto *p = a->proto;
  rte *new0 = new;
  rte *old0 = old;

  if (new)
    {
      int ok;
      if ((class & IADDR_SCOPE_MASK) < p->min_scope)
	{
	  rte_trace_out(D_FILTERS, p, new, "out of scope");
	  new = NULL;
	}
      else if ((ok = p->import_control ? p->import_control(p, &new, &tmpa, rte_update_pool) : 0) < 0)
	{
	  rte_trace_out(D_FILTERS, p, new, "rejected by protocol");
	  new = NULL;
	}
      else if (ok)
	rte_trace_out(D_FILTERS, p, new, "forced accept by protocol");
      else if (p->out_filter == FILTER_REJECT ||
	       p->out_filter && f_run(p->out_filter, &new, &tmpa, rte_update_pool, FF_FORCE_TMPATTR) > F_ACCEPT)
	{
	  rte_trace_out(D_FILTERS, p, new, "filtered out");
	  new = NULL;
	}
    }
  if (old && p->out_filter)
    {
      /* FIXME: Do we really need to filter old routes? */
      if (p->out_filter == FILTER_REJECT)
	old = NULL;
      else
	{
	  ea_list *tmpb = p->make_tmp_attrs ? p->make_tmp_attrs(old, rte_update_pool) : NULL;
	  if (f_run(p->out_filter, &old, &tmpb, rte_update_pool, FF_FORCE_TMPATTR) > F_ACCEPT)
	    old = NULL;
	}
    }
  if (p->debug & D_ROUTES)
    {
      if (new && old)
	rte_trace_out(D_ROUTES, p, new, "replaced");
      else if (new)
	rte_trace_out(D_ROUTES, p, new, "added");
      else if (old)
	rte_trace_out(D_ROUTES, p, old, "removed");
    }
  if (new || old)
    p->rt_notify(p, net, new, old, tmpa);
  if (new && new != new0)	/* Discard temporary rte's */
    rte_free(new);
  if (old && old != old0)
    rte_free(old);
}

static void
rte_announce(rtable *tab, net *net, rte *new, rte *old, ea_list *tmpa)
{
  struct announce_hook *a;
  int class = ipa_classify(net->n.prefix);

  WALK_LIST(a, tab->hooks)
    {
      ASSERT(a->proto->core_state == FS_HAPPY || a->proto->core_state == FS_FEEDING);
      do_rte_announce(a, net, new, old, tmpa, class);
    }
}

void
rt_feed_baby(struct proto *p)
{
  struct announce_hook *h;

  if (!p->ahooks)
    return;
  DBG("Announcing routes to new protocol %s\n", p->name);
  for(h=p->ahooks; h; h=h->next)
    {
      rtable *t = h->table;
      FIB_WALK(&t->fib, fn)
	{
	  net *n = (net *) fn;
	  rte *e;
	  for(e=n->routes; e; e=e->next)
	    {
	      struct proto *q = e->attrs->proto;
	      ea_list *tmpa = q->make_tmp_attrs ? q->make_tmp_attrs(e, rte_update_pool) : NULL;
	      do_rte_announce(h, n, e, NULL, tmpa, ipa_classify(n->n.prefix));
	      lp_flush(rte_update_pool);
	    }
	}
      FIB_WALK_END;
    }
}

static inline int
rte_validate(rte *e)
{
  int c;
  net *n = e->net;

  if (ipa_nonzero(ipa_and(n->n.prefix, ipa_not(ipa_mkmask(n->n.pxlen)))))
    {
      log(L_BUG "Ignoring bogus prefix %I/%d received via %s",
	  n->n.prefix, n->n.pxlen, e->attrs->proto->name);
      return 0;
    }
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
      if ((c & IADDR_SCOPE_MASK) < e->attrs->proto->min_scope)
	{
	  log(L_WARN "Ignoring %s scope route %I/%d received from %I via %s",
	      ip_scope_text(c & IADDR_SCOPE_MASK),
	      n->n.prefix, n->n.pxlen, e->attrs->from, e->attrs->proto->name);
	  return 0;
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

static void
rte_recalculate(rtable *table, net *net, struct proto *p, rte *new, ea_list *tmpa)
{
  rte *old_best = net->routes;
  rte *old = NULL;
  rte **k, *r, *s;

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
      rte_trace_in(D_ROUTES, p, new, "added [best]");
      rte_announce(table, net, new, old_best, tmpa);
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
	  rte_announce(table, net, r, old_best, tmpa);
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
	      if (!r &&
		  table->gc_counter++ >= table->config->gc_max_ops &&
		  table->gc_time + table->config->gc_min_time <= now)
		ev_schedule(table->gc_event);
	    }
	}
      if (new)				/* Link in the new non-optimal route */
	{
	  new->next = old_best->next;
	  old_best->next = new;
	  rte_trace_in(D_ROUTES, p, new, "added");
	}
      else if (old && (p->debug & D_ROUTES))
	{
	  if (old != old_best)
	    rte_trace_in(D_ROUTES, p, old, "removed");
	  else if (net->routes)
	    rte_trace_in(D_ROUTES, p, old, "removed [replaced]");
	  else
	    rte_trace_in(D_ROUTES, p, old, "removed [sole]");
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

static int rte_update_nest_cnt;		/* Nesting counter to allow recursive updates */

static inline void
rte_update_lock(void)
{
  rte_update_nest_cnt++;
}

static inline void
rte_update_unlock(void)
{
  if (!--rte_update_nest_cnt)
    lp_flush(rte_update_pool);
}

void
rte_update(rtable *table, net *net, struct proto *p, rte *new)
{
  ea_list *tmpa = NULL;

  rte_update_lock();
  if (new)
    {
      if (!rte_validate(new))
	{
	  rte_trace_in(D_FILTERS, p, new, "invalid");
	  goto drop;
	}
      if (p->in_filter == FILTER_REJECT)
	{
	  rte_trace_in(D_FILTERS, p, new, "filtered out");
	  goto drop;
	}
      if (p->make_tmp_attrs)
	tmpa = p->make_tmp_attrs(new, rte_update_pool);
      if (p->in_filter)
	{
	  ea_list *old_tmpa = tmpa;
	  int fr = f_run(p->in_filter, &new, &tmpa, rte_update_pool, 0);
	  if (fr > F_ACCEPT)
	    {
	      rte_trace_in(D_FILTERS, p, new, "filtered out");
	      goto drop;
	    }
	  if (tmpa != old_tmpa && p->store_tmp_attrs)
	    p->store_tmp_attrs(new, tmpa);
	}
      if (!(new->attrs->aflags & RTAF_CACHED)) /* Need to copy attributes */
	new->attrs = rta_lookup(new->attrs);
      new->flags |= REF_COW;
    }
  rte_recalculate(table, net, p, new, tmpa);
  rte_update_unlock();
  return;

drop:
  rte_free(new);
  rte_update_unlock();
}

void
rte_discard(rtable *t, rte *old)	/* Non-filtered route deletion, used during garbage collection */
{
  net *n = old->net;
  struct proto *p = old->attrs->proto;

  rte_update_lock();
  rte_recalculate(t, n, p, NULL, NULL);
  rte_update_unlock();
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
  struct announce_hook *a;

  debug("Dump of routing table <%s>\n", t->name);
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
  WALK_LIST(a, t->hooks)
    debug("\tAnnounces routes to protocol %s\n", a->proto->name);
  debug("\n");
}

void
rt_dump_all(void)
{
  rtable *t;

  WALK_LIST(t, routing_tables)
    rt_dump(t);
}

static int
rt_gc(void *tab)
{
  rtable *t = tab;

  DBG("Entered routing table garbage collector for %s after %d seconds and %d deletes\n",
      t->name, (int)(now - t->gc_time), t->gc_counter);
  rt_prune(t);
  return 0;
}

void
rt_setup(pool *p, rtable *t, char *name, struct rtable_config *cf)
{
  bzero(t, sizeof(*t));
  fib_init(&t->fib, p, sizeof(net), 0, rte_init);
  t->name = name;
  t->config = cf;
  init_list(&t->hooks);
  if (cf)
    {
      t->gc_event = ev_new(p);
      t->gc_event->hook = rt_gc;
      t->gc_event->data = t;
    }
}

void
rt_init(void)
{
  rta_init();
  rt_table_pool = rp_new(&root_pool, "Routing tables");
  rte_update_pool = lp_new(rt_table_pool, 4080);
  rte_slab = sl_new(rt_table_pool, sizeof(rte));
  init_list(&routing_tables);
}

void
rt_prune(rtable *tab)
{
  struct fib_iterator fit;
  int rcnt = 0, rdel = 0, ncnt = 0, ndel = 0;

  DBG("Pruning route table %s\n", tab->name);
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
	    rte_discard(tab, e);
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
  DBG("Pruned %d of %d routes and %d of %d networks\n", rcnt, rdel, ncnt, ndel);
  tab->gc_counter = 0;
  tab->gc_time = now;
}

void
rt_prune_all(void)
{
  rtable *t;

  WALK_LIST(t, routing_tables)
    rt_prune(t);
}

struct rtable_config *
rt_new_table(struct symbol *s)
{
  struct rtable_config *c = cfg_allocz(sizeof(struct rtable_config));

  cf_define_symbol(s, SYM_TABLE, c);
  c->name = s->name;
  add_tail(&new_config->tables, &c->n);
  c->gc_max_ops = 100;
  c->gc_min_time = 5;
  return c;
}

void
rt_preconfig(struct config *c)
{
  struct symbol *s = cf_find_symbol("master");

  init_list(&c->tables);
  c->master_rtc = rt_new_table(s);
}

void
rt_lock_table(rtable *r)
{
  r->use_count++;
}

void
rt_unlock_table(rtable *r)
{
  if (!--r->use_count && r->deleted)
    {
      struct config *conf = r->deleted;
      DBG("Deleting routing table %s\n", r->name);
      rem_node(&r->n);
      fib_free(&r->fib);
      mb_free(r);
      config_del_obstacle(conf);
    }
}

void
rt_commit(struct config *new, struct config *old)
{
  struct rtable_config *o, *r;

  DBG("rt_commit:\n");
  if (old)
    {
      WALK_LIST(o, old->tables)
	{
	  rtable *ot = o->table;
	  if (!ot->deleted)
	    {
	      struct symbol *sym = cf_find_symbol(o->name);
	      if (sym && sym->class == SYM_TABLE && !new->shutdown)
		{
		  DBG("\t%s: same\n", o->name);
		  r = sym->def;
		  r->table = ot;
		  ot->name = r->name;
		  ot->config = r;
		}
	      else
		{
		  DBG("\t%s: deleted\n", o->name);
		  ot->deleted = old;
		  config_add_obstacle(old);
		  rt_lock_table(ot);
		  rt_unlock_table(ot);
		}
	    }
	}
    }

  WALK_LIST(r, new->tables)
    if (!r->table)
      {
	rtable *t = mb_alloc(rt_table_pool, sizeof(struct rtable));
	DBG("\t%s: created\n", r->name);
	rt_setup(rt_table_pool, t, r->name, r);
	add_tail(&routing_tables, &t->n);
	r->table = t;
      }
  DBG("\tdone\n");
}

/*
 *  CLI commands
 */

static void
rt_format_via(rte *e, byte *via)
{
  rta *a = e->attrs;

  switch (a->dest)
    {
    case RTD_ROUTER:	bsprintf(via, "via %I on %s", a->gw, a->iface->name); break;
    case RTD_DEVICE:	bsprintf(via, "dev %s", a->iface->name); break;
    case RTD_BLACKHOLE:	bsprintf(via, "blackhole"); break;
    case RTD_UNREACHABLE:	bsprintf(via, "unreachable"); break;
    case RTD_PROHIBIT:	bsprintf(via, "prohibited"); break;
    default:		bsprintf(via, "???");
    }
}

static void
rt_show_rte(struct cli *c, byte *ia, rte *e, struct rt_show_data *d)
{
  byte via[STD_ADDRESS_P_LENGTH+32], from[STD_ADDRESS_P_LENGTH+6];
  byte tm[TM_RELTIME_BUFFER_SIZE], info[256];
  rta *a = e->attrs;

  rt_format_via(e, via);
  tm_format_reltime(tm, e->lastmod);
  if (ipa_nonzero(a->from) && !ipa_equal(a->from, a->gw))
    bsprintf(from, " from %I", a->from);
  else
    from[0] = 0;
  if (a->proto->proto->get_route_info)
    a->proto->proto->get_route_info(e, info);
  else
    bsprintf(info, " (%d)", e->pref);
  cli_printf(c, -1007, "%-18s %s [%s %s%s]%s", ia, via, a->proto->name, tm, from, info);
  if (d->verbose)
    {
      rta_show(c, a);
      if (a->proto->proto->show_route_data)
	a->proto->proto->show_route_data(e);
    }
}

static void
rt_show_net(struct cli *c, net *n, struct rt_show_data *d)
{
  rte *e, *ee;
  byte ia[STD_ADDRESS_P_LENGTH+8];

  bsprintf(ia, "%I/%d", n->n.prefix, n->n.pxlen);
  for(e=n->routes; e; e=e->next)
    {
      struct ea_list *tmpa = NULL;
      ee = e;
      rte_update_lock();		/* We use the update buffer for filtering */
      if (d->filter == FILTER_ACCEPT || f_run(d->filter, &ee, &tmpa, rte_update_pool, 0) <= F_ACCEPT)
	{
	  rt_show_rte(c, ia, e, d);
	  ia[0] = 0;
	}
      if (e != ee)
	rte_free(ee);
      rte_update_unlock();
    }
}

static void
rt_show_cont(struct cli *c)
{
  struct rt_show_data *d = c->rover;
#ifdef DEBUGGING
  unsigned max = 4;
#else
  unsigned max = 64;
#endif
  struct fib *fib = &d->table->fib;
  struct fib_iterator *it = &d->fit;

  FIB_ITERATE_START(fib, it, f)
    {
      net *n = (net *) f;
      if (!max--)
	{
	  FIB_ITERATE_PUT(it, f);
	  return;
	}
      rt_show_net(c, n, d);
    }
  FIB_ITERATE_END(f);
  cli_printf(c, 0, "");
  c->cont = c->cleanup = NULL;
}

static void
rt_show_cleanup(struct cli *c)
{
  struct rt_show_data *d = c->rover;

  /* Unlink the iterator */
  fit_get(&d->table->fib, &d->fit);
}

void
rt_show(struct rt_show_data *d)
{
  struct rtable_config *tc;
  net *n;

  if (d->pxlen == 256)
    {
      FIB_ITERATE_INIT(&d->fit, &d->table->fib);
      this_cli->cont = rt_show_cont;
      this_cli->cleanup = rt_show_cleanup;
      this_cli->rover = d;
    }
  else
    {
      n = fib_find(&d->table->fib, &d->prefix, d->pxlen);
      if (n)
	{
	  rt_show_net(this_cli, n, d);
	  cli_msg(0, "");
	}
      else
	cli_msg(8001, "Network not in table");
    }
}
