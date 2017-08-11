/*
 *	BIRD -- Router Advertisement
 *
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */


#include <stdlib.h>
#include "radv.h"

/**
 * DOC: Router Advertisements
 *
 * The RAdv protocol is implemented in two files: |radv.c| containing
 * the interface with BIRD core and the protocol logic and |packets.c|
 * handling low level protocol stuff (RX, TX and packet formats).
 * The protocol does not export any routes.
 *
 * The RAdv is structured in the usual way - for each handled interface
 * there is a structure &radv_iface that contains a state related to
 * that interface together with its resources (a socket, a timer).
 * There is also a prepared RA stored in a TX buffer of the socket
 * associated with an iface. These iface structures are created
 * and removed according to iface events from BIRD core handled by
 * radv_if_notify() callback.
 *
 * The main logic of RAdv consists of two functions:
 * radv_iface_notify(), which processes asynchronous events (specified
 * by RA_EV_* codes), and radv_timer(), which triggers sending RAs and
 * computes the next timeout.
 *
 * The RAdv protocol could receive routes (through
 * radv_import_control() and radv_rt_notify()), but only the
 * configured trigger route is tracked (in &active var).  When a radv
 * protocol is reconfigured, the connected routing table is examined
 * (in radv_check_active()) to have proper &active value in case of
 * the specified trigger prefix was changed.
 *
 * Supported standards:
 * - RFC 4861 - main RA standard
 * - RFC 6106 - DNS extensions (RDDNS, DNSSL)
 * - RFC 4191 (partial) - Default Router Preference
 */

static void
radv_timer(timer *tm)
{
  struct radv_iface *ifa = tm->data;
  struct radv_proto *p = ifa->ra;

  RADV_TRACE(D_EVENTS, "Timer fired on %s", ifa->iface->name);

  /* If some dead prefixes expired, regenerate the prefix list and the packet.
   * We do so by pretending there was a change on the interface.
   *
   * This sets the timer, but we replace it just at the end of this function
   * (replacing a timer is fine).
   */
  if (ifa->prefix_expires != 0 && ifa->prefix_expires <= now)
    radv_iface_notify(ifa, RA_EV_GC);

  radv_send_ra(ifa, 0);

  /* Update timer */
  ifa->last = now;
  unsigned after = ifa->cf->min_ra_int;
  after += random() % (ifa->cf->max_ra_int - ifa->cf->min_ra_int + 1);

  if (ifa->initial)
    ifa->initial--;

  if (ifa->initial)
    after = MIN(after, MAX_INITIAL_RTR_ADVERT_INTERVAL);

  tm_start(ifa->timer, after);
}

static struct radv_prefix_config default_prefix = {
  .onlink = 1,
  .autonomous = 1,
  .valid_lifetime = DEFAULT_VALID_LIFETIME,
  .preferred_lifetime = DEFAULT_PREFERRED_LIFETIME
};

static struct radv_prefix_config dead_prefix = {
};

/* Find a corresponding config for the given prefix */
static struct radv_prefix_config *
radv_prefix_match(struct radv_iface *ifa, struct ifa *a)
{
  struct radv_proto *p = ifa->ra;
  struct radv_config *cf = (struct radv_config *) (p->p.cf);
  struct radv_prefix_config *pc;

  if (a->scope <= SCOPE_LINK)
    return NULL;

  WALK_LIST(pc, ifa->cf->pref_list)
    if ((a->pxlen >= pc->pxlen) && ipa_in_net(a->prefix, pc->prefix, pc->pxlen))
      return pc;

  WALK_LIST(pc, cf->pref_list)
    if ((a->pxlen >= pc->pxlen) && ipa_in_net(a->prefix, pc->prefix, pc->pxlen))
      return pc;

  return &default_prefix;
}

/*
 * Go through the list of prefixes, compare them with configs and decide if we
 * want them or not. */
static void
prefixes_prepare(struct radv_iface *ifa)
{
  struct radv_proto *p = ifa->ra;
  /* First mark all the prefixes as unused */
  struct radv_prefix *pfx;

  WALK_LIST(pfx, ifa->prefixes)
    pfx->mark = 0;

  /* Now find all the prefixes we want to use and make sure they are in the
   * list. */
  struct ifa *addr;
  WALK_LIST(addr, ifa->iface->addrs)
  {
    struct radv_prefix_config *pc = radv_prefix_match(ifa, addr);

    if (!pc || pc->skip)
      continue;

    /* Do we have it already? */
    struct radv_prefix *existing = NULL;
    WALK_LIST(pfx, ifa->prefixes)
      if (pfx->len == addr->pxlen &&
	  memcmp(&pfx->prefix, &addr->prefix, sizeof pfx->prefix) == 0)
      {
	existing = pfx;
	break;
      }

    if (!existing)
    {
      RADV_TRACE(D_EVENTS, "Allocating new prefix %I on %s", addr->prefix,
		 ifa->iface->name);
      existing = mb_allocz(ifa->pool, sizeof *existing);
      existing->prefix = addr->prefix;
      existing->len = addr->pxlen;
      add_tail(&ifa->prefixes, NODE existing);
    }
    /*
     * Update the information (it may have changed, or even bring a prefix back
     * to life).
     */
    existing->alive = 1;
    existing->mark = 1;
    existing->config = pc;
  }

  /*
   * Garbage-collect the prefixes. If something isn't used, it dies (but isn't
   * dropped just yet). If something is dead and rots there for long enough,
   * clean it up.
   */
  // XXX: Make these 5 minutes it configurable
  bird_clock_t rotten = now + 300;
  struct radv_prefix *next;
  bird_clock_t expires_soonest = 0;
  WALK_LIST_DELSAFE(pfx, next, ifa->prefixes) {
    if (pfx->alive && !pfx->mark)
    {
      RADV_TRACE(D_EVENTS, "Marking prefix %I on %s as dead", pfx->prefix,
		 ifa->iface->name);
      // It just died
      pfx->alive = 0;
      pfx->expires = rotten;
      pfx->config = &dead_prefix;
    }
    if (!pfx->alive)
      if (pfx->expires <= now)
      {
	RADV_TRACE(D_EVENTS, "Dropping long dead prefix %I on %s", pfx->prefix,
		   ifa->iface->name);
	// It's dead and rotten, clean it up
	rem_node(NODE pfx);
	mb_free(pfx);
      }
      else
      {
	ASSERT(pfx->expires != 0);
	// Let it rot for a while more, but look when it's ripe.
	if (expires_soonest == 0 || pfx->expires < expires_soonest)
	  expires_soonest = pfx->expires;
      }
  }
  ifa->prefix_expires = expires_soonest;
}

static char* ev_name[] = { NULL, "Init", "Change", "RS", "Garbage collect" };

void
radv_iface_notify(struct radv_iface *ifa, int event)
{
  struct radv_proto *p = ifa->ra;

  if (!ifa->sk)
    return;

  RADV_TRACE(D_EVENTS, "Event %s on %s", ev_name[event], ifa->iface->name);

  switch (event)
  {
  case RA_EV_CHANGE:
  case RA_EV_GC:
    ifa->plen = 0;
  case RA_EV_INIT:
    ifa->initial = MAX_INITIAL_RTR_ADVERTISEMENTS;
    break;

  case RA_EV_RS:
    break;
  }

  prefixes_prepare(ifa);

  /* Update timer */
  unsigned delta = now - ifa->last;
  unsigned after = 0;

  if (delta < ifa->cf->min_delay)
    after = ifa->cf->min_delay - delta;

  tm_start(ifa->timer, after);
}

static void
radv_iface_notify_all(struct radv_proto *p, int event)
{
  struct radv_iface *ifa;

  WALK_LIST(ifa, p->iface_list)
    radv_iface_notify(ifa, event);
}


static struct radv_iface *
radv_iface_find(struct radv_proto *p, struct iface *what)
{
  struct radv_iface *ifa;

  WALK_LIST(ifa, p->iface_list)
    if (ifa->iface == what)
      return ifa;

  return NULL;
}

static void
radv_iface_add(struct object_lock *lock)
{
  struct radv_iface *ifa = lock->data;
  struct radv_proto *p = ifa->ra;

  if (! radv_sk_open(ifa))
  {
    log(L_ERR "%s: Socket open failed on interface %s", p->p.name, ifa->iface->name);
    return;
  }

  radv_iface_notify(ifa, RA_EV_INIT);
}

static inline struct ifa *
find_lladdr(struct iface *iface)
{
  struct ifa *a;
  WALK_LIST(a, iface->addrs)
    if (a->scope == SCOPE_LINK)
      return a;

  return NULL;
}

static void
radv_iface_new(struct radv_proto *p, struct iface *iface, struct radv_iface_config *cf)
{
  struct radv_iface *ifa;

  RADV_TRACE(D_EVENTS, "Adding interface %s", iface->name);

  pool *pool = rp_new(p->p.pool, iface->name);
  ifa = mb_allocz(pool, sizeof(struct radv_iface));
  ifa->pool = pool;
  ifa->ra = p;
  ifa->cf = cf;
  ifa->iface = iface;
  init_list(&ifa->prefixes);

  add_tail(&p->iface_list, NODE ifa);

  ifa->addr = find_lladdr(iface);
  if (!ifa->addr)
  {
    log(L_ERR "%s: Missing link-local address on interface %s", p->p.name, iface->name);
    return;
  }

  timer *tm = tm_new(pool);
  tm->hook = radv_timer;
  tm->data = ifa;
  tm->randomize = 0;
  tm->recurrent = 0;
  ifa->timer = tm;

  struct object_lock *lock = olock_new(pool);
  lock->addr = IPA_NONE;
  lock->type = OBJLOCK_IP;
  lock->port = ICMPV6_PROTO;
  lock->iface = iface;
  lock->data = ifa;
  lock->hook = radv_iface_add;
  ifa->lock = lock;

  olock_acquire(lock);
}

static void
radv_iface_remove(struct radv_iface *ifa)
{
  struct radv_proto *p = ifa->ra;
  RADV_TRACE(D_EVENTS, "Removing interface %s", ifa->iface->name);

  rem_node(NODE ifa);

  rfree(ifa->pool);
}

static void
radv_if_notify(struct proto *P, unsigned flags, struct iface *iface)
{
  struct radv_proto *p = (struct radv_proto *) P;
  struct radv_config *cf = (struct radv_config *) (P->cf);

  if (iface->flags & IF_IGNORE)
    return;

  if (flags & IF_CHANGE_UP)
  {
    struct radv_iface_config *ic = (struct radv_iface_config *)
      iface_patt_find(&cf->patt_list, iface, NULL);

    if (ic)
      radv_iface_new(p, iface, ic);

    return;
  }

  struct radv_iface *ifa = radv_iface_find(p, iface);
  if (!ifa)
    return;

  if (flags & IF_CHANGE_DOWN)
  {
    radv_iface_remove(ifa);
    return;
  }

  if ((flags & IF_CHANGE_LINK) && (iface->flags & IF_LINK_UP))
    radv_iface_notify(ifa, RA_EV_INIT);
}

static void
radv_ifa_notify(struct proto *P, unsigned flags UNUSED, struct ifa *a)
{
  struct radv_proto *p = (struct radv_proto *) P;

  if (a->flags & IA_SECONDARY)
    return;

  if (a->scope <= SCOPE_LINK)
    return;

  struct radv_iface *ifa = radv_iface_find(p, a->iface);

  if (ifa)
    radv_iface_notify(ifa, RA_EV_CHANGE);
}

static inline int radv_net_match_trigger(struct radv_config *cf, net *n)
{
  return cf->trigger_valid &&
    (n->n.pxlen == cf->trigger_pxlen) &&
    ipa_equal(n->n.prefix, cf->trigger_prefix);
}

int
radv_import_control(struct proto *P, rte **new, ea_list **attrs UNUSED, struct linpool *pool UNUSED)
{
  // struct radv_proto *p = (struct radv_proto *) P;
  struct radv_config *cf = (struct radv_config *) (P->cf);

  if (radv_net_match_trigger(cf, (*new)->net))
    return RIC_PROCESS;

  return RIC_DROP;
}

static void
radv_rt_notify(struct proto *P, rtable *tbl UNUSED, net *n, rte *new, rte *old UNUSED, ea_list *attrs UNUSED)
{
  struct radv_proto *p = (struct radv_proto *) P;
  struct radv_config *cf = (struct radv_config *) (P->cf);

  if (radv_net_match_trigger(cf, n))
  {
    u8 old_active = p->active;
    p->active = !!new;

    if (p->active == old_active)
      return;

    if (p->active)
      RADV_TRACE(D_EVENTS, "Triggered");
    else
      RADV_TRACE(D_EVENTS, "Suppressed");

    radv_iface_notify_all(p, RA_EV_CHANGE);
  }
}

static int
radv_check_active(struct radv_proto *p)
{
  struct radv_config *cf = (struct radv_config *) (p->p.cf);

  if (! cf->trigger_valid)
    return 1;

  return rt_examine(p->p.table, cf->trigger_prefix, cf->trigger_pxlen,
		    &(p->p), p->p.cf->out_filter);
}

static struct proto *
radv_init(struct proto_config *c)
{
  struct proto *P = proto_new(c, sizeof(struct radv_proto));

  P->accept_ra_types = RA_OPTIMAL;
  P->import_control = radv_import_control;
  P->rt_notify = radv_rt_notify;
  P->if_notify = radv_if_notify;
  P->ifa_notify = radv_ifa_notify;

  return P;
}

static int
radv_start(struct proto *P)
{
  struct radv_proto *p = (struct radv_proto *) P;
  struct radv_config *cf = (struct radv_config *) (P->cf);

  init_list(&(p->iface_list));
  p->active = !cf->trigger_valid;

  return PS_UP;
}

static inline void
radv_iface_shutdown(struct radv_iface *ifa)
{
  if (ifa->sk)
    radv_send_ra(ifa, 1);
}

static int
radv_shutdown(struct proto *P)
{
  struct radv_proto *p = (struct radv_proto *) P;

  struct radv_iface *ifa;
  WALK_LIST(ifa, p->iface_list)
    radv_iface_shutdown(ifa);

  return PS_DOWN;
}

static int
radv_reconfigure(struct proto *P, struct proto_config *c)
{
  struct radv_proto *p = (struct radv_proto *) P;
  // struct radv_config *old = (struct radv_config *) (p->cf);
  struct radv_config *new = (struct radv_config *) c;

  /*
   * The question is why there is a reconfigure function for RAdv if
   * it has almost none internal state so restarting the protocol
   * would probably suffice. One small reason is that restarting the
   * protocol would lead to sending a RA with Router Lifetime 0
   * causing nodes to temporary remove their default routes.
   */

  P->cf = c; /* radv_check_active() requires proper P->cf */
  p->active = radv_check_active(p);

  struct iface *iface;
  WALK_LIST(iface, iface_list)
  {
    struct radv_iface *ifa = radv_iface_find(p, iface);
    struct radv_iface_config *ic = (struct radv_iface_config *)
      iface_patt_find(&new->patt_list, iface, NULL);

    if (ifa && ic)
    {
      ifa->cf = ic;

      /* We cheat here - always notify the change even if there isn't
	 any. That would leads just to a few unnecessary RAs. */
      radv_iface_notify(ifa, RA_EV_CHANGE);
    }

    if (ifa && !ic)
    {
      radv_iface_shutdown(ifa);
      radv_iface_remove(ifa);
    }

    if (!ifa && ic)
      radv_iface_new(p, iface, ic);
  }

  return 1;
}

static void
radv_copy_config(struct proto_config *dest, struct proto_config *src)
{
  struct radv_config *d = (struct radv_config *) dest;
  struct radv_config *s = (struct radv_config *) src;

  /* We clean up patt_list, ifaces are non-sharable */
  init_list(&d->patt_list);

  /* We copy pref_list, shallow copy suffices */
  cfg_copy_list(&d->pref_list, &s->pref_list, sizeof(struct radv_prefix_config));
}

static void
radv_get_status(struct proto *P, byte *buf)
{
  struct radv_proto *p = (struct radv_proto *) P;

  if (!p->active)
    strcpy(buf, "Suppressed");
}

struct protocol proto_radv = {
  .name =		"RAdv",
  .template =		"radv%d",
  .config_size =	sizeof(struct radv_config),
  .init =		radv_init,
  .start =		radv_start,
  .shutdown =		radv_shutdown,
  .reconfigure =	radv_reconfigure,
  .copy_config =	radv_copy_config,
  .get_status =		radv_get_status
};
