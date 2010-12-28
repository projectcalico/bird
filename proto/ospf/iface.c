/*
 *	BIRD -- OSPF
 *
 *	(c) 1999--2005 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

char *ospf_is[] = { "down", "loop", "waiting", "ptp", "drother",
  "backup", "dr"
};

char *ospf_ism[] = { "interface up", "wait timer fired", "backup seen",
  "neighbor change", "loop indicated", "unloop indicated", "interface down"
};

char *ospf_it[] = { "broadcast", "nbma", "ptp", "ptmp", "virtual link" };

static void
poll_timer_hook(timer * timer)
{
  ospf_hello_send(timer, 1, NULL);
}

static void
hello_timer_hook(timer * timer)
{
  ospf_hello_send(timer, 0, NULL);
}

static void
wait_timer_hook(timer * timer)
{
  struct ospf_iface *ifa = (struct ospf_iface *) timer->data;
  struct proto *p = &ifa->oa->po->proto;

  OSPF_TRACE(D_EVENTS, "Wait timer fired on interface %s.", ifa->iface->name);
  ospf_iface_sm(ifa, ISM_WAITF);
}

u32
rxbufsize(struct ospf_iface *ifa)
{
  switch(ifa->rxbuf)
  {
    case OSPF_RXBUF_NORMAL:
      return (ifa->iface->mtu * 2);
      break;
    case OSPF_RXBUF_LARGE:
      return OSPF_MAX_PKT_SIZE;
      break;
    default:
      return ifa->rxbuf;
      break;
  }
}

struct nbma_node *
find_nbma_node_in(list *nnl, ip_addr ip)
{
  struct nbma_node *nn;
  WALK_LIST(nn, *nnl)
    if (ipa_equal(nn->ip, ip))
      return nn;
  return NULL;
}

static int
ospf_sk_open(struct ospf_iface *ifa, int multicast)
{
  sock *sk = sk_new(ifa->pool);
  sk->type = SK_IP;
  sk->dport = OSPF_PROTO;
  sk->saddr = IPA_NONE;

  sk->tos = IP_PREC_INTERNET_CONTROL;
  sk->rx_hook = ospf_rx_hook;
  sk->tx_hook = ospf_tx_hook;
  sk->err_hook = ospf_err_hook;
  sk->iface = ifa->iface;
  sk->rbsize = rxbufsize(ifa);
  sk->tbsize = rxbufsize(ifa);
  sk->data = (void *) ifa;
  sk->flags = SKF_LADDR_RX;

  if (sk_open(sk) != 0)
    goto err;

#ifdef OSPFv3
  /* 12 is an offset of the checksum in an OSPF packet */
  if (sk_set_ipv6_checksum(sk, 12) < 0)
    goto err;
#endif

  /*
   * For OSPFv2: When sending a packet, it is important to have a
   * proper source address. We expect that when we send one-hop
   * unicast packets, OS chooses a source address according to the
   * destination address (to be in the same prefix). We also expect
   * that when we send multicast packets, OS uses the source address
   * from sk->saddr registered to OS by sk_setup_multicast(). This
   * behavior is needed to implement multiple virtual ifaces (struct
   * ospf_iface) on one physical iface and is signalized by
   * CONFIG_MC_PROPER_SRC.
   *
   * If this behavior is not available (for example on BSD), we create
   * non-stub iface just for the primary IP address (see
   * ospf_iface_stubby()) and we expect OS to use primary IP address
   * as a source address for both unicast and multicast packets.
   *
   * FIXME: the primary IP address is currently just the
   * lexicographically smallest address on an interface, it should be
   * signalized by sysdep code which one is really the primary.
   */

  sk->saddr = ifa->addr->ip;
  if (multicast)
  {
    if (sk_setup_multicast(sk) < 0)
      goto err;

    if (sk_join_group(sk, AllSPFRouters) < 0)
      goto err;
  }

  ifa->sk = sk;
  ifa->sk_dr = 0;
  return 1;

 err:
  rfree(sk);
  return 0;
}

static inline void
ospf_sk_join_dr(struct ospf_iface *ifa)
{
  if (ifa->sk_dr)
    return;

  sk_join_group(ifa->sk, AllDRouters);
  ifa->sk_dr = 1;
}
static inline void
ospf_sk_leave_dr(struct ospf_iface *ifa)
{
  if (!ifa->sk_dr)
    return;

  sk_leave_group(ifa->sk, AllDRouters);
  ifa->sk_dr = 0;
}

static void
ospf_iface_down(struct ospf_iface *ifa)
{
  struct ospf_neighbor *n, *nx;
  struct proto_ospf *po = ifa->oa->po;
  struct proto *p = &po->proto;
  struct ospf_iface *iff;

  if (ifa->type != OSPF_IT_VLINK)
  {
    OSPF_TRACE(D_EVENTS, "Removing interface %s", ifa->iface->name);

    /* First of all kill all the related vlinks */
    WALK_LIST(iff, po->iface_list)
    {
      if ((iff->type == OSPF_IT_VLINK) && (iff->vifa == ifa))
	ospf_iface_sm(iff, ISM_DOWN);
    }
  }

  WALK_LIST_DELSAFE(n, nx, ifa->neigh_list)
  {
    OSPF_TRACE(D_EVENTS, "Removing neighbor %I", n->ip);
    ospf_neigh_remove(n);
  }

  if (ifa->hello_timer)
    tm_stop(ifa->hello_timer);

  if (ifa->poll_timer)
    tm_stop(ifa->poll_timer);

  if (ifa->wait_timer)
    tm_stop(ifa->wait_timer);

  if (ifa->type == OSPF_IT_VLINK)
  {
    ifa->vifa = NULL;
    ifa->iface = NULL;
    ifa->addr = NULL;
    ifa->sk = NULL;
    ifa->cost = 0;
    ifa->vip = IPA_NONE;
  }

  ifa->rt_pos_beg = 0;
  ifa->rt_pos_end = 0;
#ifdef OSPFv3
  ifa->px_pos_beg = 0;
  ifa->px_pos_end = 0;
#endif
}


static void
ospf_iface_remove(struct ospf_iface *ifa)
{
  ospf_iface_sm(ifa, ISM_DOWN);
  rem_node(NODE ifa);
  rfree(ifa->pool);
}

/**
 * ospf_iface_chstate - handle changes of interface state
 * @ifa: OSPF interface
 * @state: new state
 *
 * Many actions must be taken according to interface state changes. New network
 * LSAs must be originated, flushed, new multicast sockets to listen for messages for
 * %ALLDROUTERS have to be opened, etc.
 */
void
ospf_iface_chstate(struct ospf_iface *ifa, u8 state)
{
  struct proto_ospf *po = ifa->oa->po;
  struct proto *p = &po->proto;
  u8 oldstate = ifa->state;

  if (oldstate == state)
    return;

  ifa->state = state;

  if (ifa->type == OSPF_IT_VLINK)
    OSPF_TRACE(D_EVENTS, "Changing state of virtual link %R from %s to %s",
	       ifa->vid, ospf_is[oldstate], ospf_is[state]);
  else
    OSPF_TRACE(D_EVENTS, "Changing state of iface %s from %s to %s",
	       ifa->iface->name, ospf_is[oldstate], ospf_is[state]);

  if (ifa->type == OSPF_IT_BCAST)
  {
    if ((state == OSPF_IS_BACKUP) || (state == OSPF_IS_DR))
      ospf_sk_join_dr(ifa);
    else
      ospf_sk_leave_dr(ifa);
  }

  if ((oldstate == OSPF_IS_DR) && (ifa->net_lsa != NULL))
  {
    ifa->net_lsa->lsa.age = LSA_MAXAGE;
    if (state >= OSPF_IS_WAITING)
      ospf_lsupd_flush_nlsa(po, ifa->net_lsa);

    if (can_flush_lsa(po))
      flush_lsa(ifa->net_lsa, po);
    ifa->net_lsa = NULL;
  }

  if ((oldstate > OSPF_IS_LOOP) && (state <= OSPF_IS_LOOP))
    ospf_iface_down(ifa);

  schedule_rt_lsa(ifa->oa);
  // FIXME flushling of link LSA
}

/**
 * ospf_iface_sm - OSPF interface state machine
 * @ifa: OSPF interface
 * @event: event comming to state machine
 *
 * This fully respects 9.3 of RFC 2328 except we have slightly
 * different handling of %DOWN and %LOOP state. We remove intefaces
 * that are %DOWN. %DOWN state is used when an interface is waiting
 * for a lock. %LOOP state is used when an interface does not have a
 * link.
 */
void
ospf_iface_sm(struct ospf_iface *ifa, int event)
{
  DBG("SM on %s %s. Event is '%s'\n", (ifa->type == OSPF_IT_VLINK) ? "vlink" : "iface",
    ifa->iface ? ifa->iface->name : "(none)" , ospf_ism[event]);

  switch (event)
  {
  case ISM_UP:
    if (ifa->state <= OSPF_IS_LOOP)
    {
      /* Now, nothing should be adjacent */
      if ((ifa->type == OSPF_IT_PTP) || (ifa->type == OSPF_IT_PTMP) || (ifa->type == OSPF_IT_VLINK))
      {
	ospf_iface_chstate(ifa, OSPF_IS_PTP);
      }
      else
      {
	if (ifa->priority == 0)
	  ospf_iface_chstate(ifa, OSPF_IS_DROTHER);
	else
	{
	  ospf_iface_chstate(ifa, OSPF_IS_WAITING);
	  tm_start(ifa->wait_timer, ifa->waitint);
	}
      }

      tm_start(ifa->hello_timer, ifa->helloint);

      if (ifa->poll_timer)
	tm_start(ifa->poll_timer, ifa->pollint);

      hello_timer_hook(ifa->hello_timer);
      schedule_link_lsa(ifa);
    }
    break;

  case ISM_BACKS:
  case ISM_WAITF:
    if (ifa->state == OSPF_IS_WAITING)
    {
      bdr_election(ifa);
    }
    break;

  case ISM_NEICH:
    if ((ifa->state == OSPF_IS_DROTHER) || (ifa->state == OSPF_IS_DR) ||
	(ifa->state == OSPF_IS_BACKUP))
    {
      bdr_election(ifa);
      schedule_rt_lsa(ifa->oa);
    }
    break;

  case ISM_LOOP:
    if (ifa->sk && ifa->check_link)
      ospf_iface_chstate(ifa, OSPF_IS_LOOP);
    break;

  case ISM_UNLOOP:
    /* Immediate go UP */
    if (ifa->state == OSPF_IS_LOOP)
      ospf_iface_sm(ifa, ISM_UP);
    break;

  case ISM_DOWN:
    ospf_iface_chstate(ifa, OSPF_IS_DOWN);
    break;

  default:
    bug("OSPF_I_SM - Unknown event?");
    break;
  }

}

static u8
ospf_iface_classify(struct iface *ifa, struct ifa *addr)
{
  if (ipa_nonzero(addr->opposite))
    return (ifa->flags & IF_MULTICAST) ? OSPF_IT_PTP :  OSPF_IT_PTMP;

  if ((ifa->flags & (IF_MULTIACCESS | IF_MULTICAST)) ==
      (IF_MULTIACCESS | IF_MULTICAST))
    return OSPF_IT_BCAST;

  if ((ifa->flags & (IF_MULTIACCESS | IF_MULTICAST)) == IF_MULTIACCESS)
    return OSPF_IT_NBMA;

  return OSPF_IT_PTP;
}

struct ospf_iface *
ospf_iface_find(struct proto_ospf *p, struct iface *what)
{
  struct ospf_iface *i;

  WALK_LIST(i, p->iface_list) if ((i->iface == what) && (i->type != OSPF_IT_VLINK))
    return i;
  return NULL;
}

static void
ospf_iface_add(struct object_lock *lock)
{
  struct ospf_iface *ifa = lock->data;
  struct proto_ospf *po = ifa->oa->po;
  struct proto *p = &po->proto;

  int mc = (ifa->type == OSPF_IT_BCAST) || (ifa->type == OSPF_IT_PTP);
  if (! ospf_sk_open(ifa, mc))
  {
    log(L_ERR "%s: Socket open failed on interface %s, declaring as stub", p->name, ifa->iface->name);
    ifa->ioprob = OSPF_I_SK;
    ifa->stub = 1;
  }

  /* Do iface UP, unless there is no link and we use link detection */
  ospf_iface_sm(ifa, (ifa->check_link && !(ifa->iface->flags & IF_LINK_UP)) ? ISM_LOOP : ISM_UP);
}

void
ospf_iface_new(struct proto_ospf *po, struct iface *iface, struct ifa *addr,
	       struct ospf_area_config *ac, struct ospf_iface_patt *ip)
{
  struct proto *p = &po->proto;
  struct pool *pool = rp_new(p->pool, "OSPF Interface");
  struct ospf_iface *ifa;
  struct nbma_node *nbma, *nb;
  struct object_lock *lock;
  struct ospf_area *oa;

  if (ip->type != OSPF_IT_VLINK)
    OSPF_TRACE(D_EVENTS, "Adding interface %s", iface->name);

  ifa = mb_allocz(pool, sizeof(struct ospf_iface));
  ifa->iface = iface;
  ifa->addr = addr;
  ifa->pool = pool;

  ifa->cost = ip->cost;
  ifa->rxmtint = ip->rxmtint;
  ifa->inftransdelay = ip->inftransdelay;
  ifa->priority = ip->priority;
  ifa->helloint = ip->helloint;
  ifa->pollint = ip->pollint;
  ifa->strictnbma = ip->strictnbma;
  ifa->waitint = ip->waitint;
  ifa->dead = (ip->dead == 0) ? ip->deadc * ifa->helloint : ip->dead;
  ifa->stub = ospf_iface_stubby(ip, addr);
  ifa->ioprob = OSPF_I_OK;
  ifa->rxbuf = ip->rxbuf;
  ifa->check_link = ip->check_link;
  ifa->ecmp_weight = ip->ecmp_weight;

#ifdef OSPFv2
  ifa->autype = ip->autype;
  ifa->passwords = ip->passwords;
#endif

#ifdef OSPFv3
  ifa->instance_id = ip->instance_id;
#endif

  if (ip->type == OSPF_IT_UNDEF)
    ifa->type = ospf_iface_classify(iface, addr);
  else
    ifa->type = ip->type;

  /* a loopback/dummy address */
  if ((addr->pxlen == MAX_PREFIX_LENGTH) && ipa_zero(addr->opposite))
    ifa->stub = 1;

  /* Check validity of interface type */
  int old_type = ifa->type;

#ifdef OSPFv2
  if ((ifa->type == OSPF_IT_BCAST) && (addr->flags & IA_UNNUMBERED))
    ifa->type = OSPF_IT_PTP;

  if ((ifa->type == OSPF_IT_NBMA) && (addr->flags & IA_UNNUMBERED))
    ifa->type = OSPF_IT_PTMP;
#endif

  if ((ifa->type == OSPF_IT_BCAST) && !(iface->flags & IF_MULTICAST))
    ifa->type = OSPF_IT_NBMA;

  if ((ifa->type == OSPF_IT_PTP) && !(iface->flags & IF_MULTICAST))
    ifa->type = OSPF_IT_PTMP;

  if (ifa->type != old_type)
    log(L_WARN "%s: Cannot use interface %s as %s, forcing %s",
	p->name, iface->name, ospf_it[old_type], ospf_it[ifa->type]);


  init_list(&ifa->neigh_list);
  init_list(&ifa->nbma_list);

  WALK_LIST(nb, ip->nbma_list)
  {
    if (!ipa_in_net(nb->ip, addr->prefix, addr->pxlen))
      continue;

    nbma = mb_alloc(pool, sizeof(struct nbma_node));
    nbma->ip = nb->ip;
    nbma->eligible = nb->eligible;
    nbma->found = 0;
    add_tail(&ifa->nbma_list, NODE nbma);
  }

  DBG("%s: Installing hello timer. (%u)\n", p->name, ifa->helloint);
  ifa->hello_timer = tm_new(pool);
  ifa->hello_timer->data = ifa;
  ifa->hello_timer->randomize = 0;
  ifa->hello_timer->hook = hello_timer_hook;
  ifa->hello_timer->recurrent = ifa->helloint;

  if (ifa->type == OSPF_IT_NBMA)
  {
    DBG("%s: Installing poll timer. (%u)\n", p->name, ifa->pollint);
    ifa->poll_timer = tm_new(pool);
    ifa->poll_timer->data = ifa;
    ifa->poll_timer->randomize = 0;
    ifa->poll_timer->hook = poll_timer_hook;
    ifa->poll_timer->recurrent = ifa->pollint;
  }

  if ((ifa->type == OSPF_IT_BCAST) || (ifa->type == OSPF_IT_NBMA))
  {
    DBG("%s: Installing wait timer. (%u)\n", p->name, ifa->waitint);
    ifa->wait_timer = tm_new(pool);
    ifa->wait_timer->data = ifa;
    ifa->wait_timer->randomize = 0;
    ifa->wait_timer->hook = wait_timer_hook;
    ifa->wait_timer->recurrent = 0;
  }

  ifa->state = OSPF_IS_DOWN;
  add_tail(&po->iface_list, NODE ifa);

  ifa->oa = NULL;
  WALK_LIST(oa, po->area_list)
  {
    if (oa->areaid == ac->areaid)
    {
      ifa->oa = oa;
      break;
    }
  }

  if (!ifa->oa)
    bug("Cannot add any area to accepted Interface");
  else

  if (ifa->type == OSPF_IT_VLINK)
  {
    ifa->oa = po->backbone;
    ifa->voa = oa;
    ifa->vid = ip->vid;
    return;			/* Don't lock, don't add sockets */
  }

  /*
   * In some cases we allow more ospf_ifaces on one physical iface.
   * In OSPFv2, if they use different IP address prefix.
   * In OSPFv3, if they use different instance_id.
   * Therefore, we store such info to lock->addr field.
   */

  lock = olock_new(pool);
#ifdef OSPFv2
  lock->addr = ifa->addr->prefix;
#else /* OSPFv3 */
  lock->addr = _MI(0,0,0,ifa->instance_id);
#endif
  lock->type = OBJLOCK_IP;
  lock->port = OSPF_PROTO;
  lock->iface = iface;
  lock->data = ifa;
  lock->hook = ospf_iface_add;

  olock_acquire(lock);
}


#ifdef OSPFv2

void
ospf_ifa_notify(struct proto *p, unsigned flags, struct ifa *a)
{
  struct proto_ospf *po = (struct proto_ospf *) p;
  struct ospf_config *cf = (struct ospf_config *) (p->cf);

  if (a->flags & IA_SECONDARY)
    return;

  if (a->scope <= SCOPE_LINK)
    return;

  /* In OSPFv2, we create OSPF iface for each address. */
  if (flags & IF_CHANGE_UP)
  {
    int done = 0;
    struct ospf_area_config *ac;
    WALK_LIST(ac, cf->area_list)
    {
      struct ospf_iface_patt *ip = (struct ospf_iface_patt *)
	iface_patt_find(&ac->patt_list, a->iface, a);

      if (ip)
      {
	if (!done)
	  ospf_iface_new(po, a->iface, a, ac, ip);
	done++;
      }
    }

    if (done > 1)
      log(L_WARN "%s: Interface %s (IP %I) matches for multiple areas", p->name,  a->iface->name, a->ip);
  }

  if (flags & IF_CHANGE_DOWN)
  {
    struct ospf_iface *ifa, *ifx;
    WALK_LIST_DELSAFE(ifa, ifx, po->iface_list)
    {
      if ((ifa->type != OSPF_IT_VLINK) && (ifa->addr == a))
	ospf_iface_remove(ifa);
      /* See a note in ospf_iface_notify() */
    }
  }
}

#else /* OSPFv3 */

static inline int iflag_test(u32 *a, u8 i)
{
  return a[i / 32] & (1u << (i % 32));
}

static inline void iflag_set(u32 *a, u8 i)
{
  a[i / 32] |= (1u << (i % 32));
}

void
ospf_ifa_notify(struct proto *p, unsigned flags, struct ifa *a)
{
  struct proto_ospf *po = (struct proto_ospf *) p;
  struct ospf_config *cf = (struct ospf_config *) (p->cf);

  if (a->flags & IA_SECONDARY)
    return;

  if (a->scope < SCOPE_LINK)
    return;

  /* In OSPFv3, we create OSPF iface for link-local address,
     other addresses are used for link-LSA. */
  if (a->scope == SCOPE_LINK)
  {
    if (flags & IF_CHANGE_UP)
    {
      u32 found_all[8] = {};
      struct ospf_area_config *ac;

      WALK_LIST(ac, cf->area_list)
      {
	u32 found_new[8] = {};
	struct iface_patt *pt;

	WALK_LIST(pt, ac->patt_list)
	{
	  if (iface_patt_match(pt, a->iface, a))
	  {
	    struct ospf_iface_patt *ipt = (struct ospf_iface_patt *) pt;

	    /* If true, we already assigned that IID and we skip
	       this to implement first-match behavior */
	    if (iflag_test(found_new, ipt->instance_id))
	      continue;

	    /* If true, we already assigned that in a different area,
	       we log collision */
	    if (iflag_test(found_all, ipt->instance_id))
	    {
	      log(L_WARN "%s: Interface %s (IID %d) matches for multiple areas",
		  p->name,  a->iface->name, ipt->instance_id);
	      continue;
	    }

	    iflag_set(found_all, ipt->instance_id);
	    iflag_set(found_new, ipt->instance_id);
	    ospf_iface_new(po, a->iface, a, ac, ipt);
	  }
	}
      }
    }

    if (flags & IF_CHANGE_DOWN)
    {
      struct ospf_iface *ifa, *ifx;
      WALK_LIST_DELSAFE(ifa, ifx, po->iface_list)
      {
	if ((ifa->type != OSPF_IT_VLINK) && (ifa->addr == a))
	  ospf_iface_remove(ifa);
	/* See a note in ospf_iface_notify() */
      }
    }
  }
  else
  {
    struct ospf_iface *ifa;
    WALK_LIST(ifa, po->iface_list)
    {
      if (ifa->iface == a->iface)
      {
	schedule_rt_lsa(ifa->oa);
	/* Event 5 from RFC5340 4.4.3. */
	schedule_link_lsa(ifa);
	return;
      }
    }
  }
}

#endif

void
ospf_iface_change_mtu(struct proto_ospf *po, struct ospf_iface *ifa)
{
  struct proto *p = &po->proto;
  struct ospf_packet *op;
  struct ospf_neighbor *n;
  OSPF_TRACE(D_EVENTS, "Changing MTU on interface %s.", ifa->iface->name);

  if (ifa->sk)
  {
    ifa->sk->rbsize = rxbufsize(ifa);
    ifa->sk->tbsize = rxbufsize(ifa);
    sk_reallocate(ifa->sk);
  }

  WALK_LIST(n, ifa->neigh_list)
  {
    op = (struct ospf_packet *) n->ldbdes;
    n->ldbdes = mb_allocz(n->pool, ifa->iface->mtu);

    if (ntohs(op->length) <= ifa->iface->mtu)	/* If the packet in old buffer is bigger, let it filled by zeros */
      memcpy(n->ldbdes, op, ifa->iface->mtu);	/* If the packet is old is same or smaller, copy it */

    mb_free(op);
  }
}

static void
ospf_iface_notify(struct proto_ospf *po, unsigned flags, struct ospf_iface *ifa)
{
  if (flags & IF_CHANGE_DOWN)
  {
    ospf_iface_remove(ifa);
    return;
  }

  if (flags & IF_CHANGE_LINK)
    ospf_iface_sm(ifa, (ifa->iface->flags & IF_LINK_UP) ? ISM_UNLOOP : ISM_LOOP);

  if (flags & IF_CHANGE_MTU)
    ospf_iface_change_mtu(po, ifa);
}

void
ospf_if_notify(struct proto *p, unsigned flags, struct iface *iface)
{
  struct proto_ospf *po = (struct proto_ospf *) p;
    
  if (iface->flags & IF_IGNORE)
    return;

  /* Going up means that there are no such ifaces yet */ 
  if (flags & IF_CHANGE_UP)
    return;

  struct ospf_iface *ifa, *ifx;
  WALK_LIST_DELSAFE(ifa, ifx, po->iface_list)
    if ((ifa->type != OSPF_IT_VLINK) && (ifa->iface == iface))
      ospf_iface_notify(po, flags, ifa);

  /* We use here that even shutting down iface also shuts down
     the vlinks, but vlinks are not freed and stays in the
     iface_list even when down */
}

void
ospf_iface_info(struct ospf_iface *ifa)
{
  char *strict = "";

  if (ifa->strictnbma &&
      ((ifa->type == OSPF_IT_NBMA) || (ifa->type == OSPF_IT_PTMP)))
    strict = "(strict)";

  if (ifa->type == OSPF_IT_VLINK)
  {
    cli_msg(-1015, "Virtual link to %R:", ifa->vid);
    cli_msg(-1015, "\tPeer IP: %I", ifa->vip);
    cli_msg(-1015, "\tTransit area: %R (%u)", ifa->voa->areaid,
	    ifa->voa->areaid);
    cli_msg(-1015, "\tInterface: \"%s\"",
	    (ifa->iface ? ifa->iface->name : "(none)"));
  }
  else
  {
#ifdef OSPFv2
    if (ifa->addr->flags & IA_UNNUMBERED)
      cli_msg(-1015, "Interface %s (peer %I)", ifa->iface->name, ifa->addr->opposite);
    else
      cli_msg(-1015, "Interface %s (%I/%d)", ifa->iface->name, ifa->addr->prefix, ifa->addr->pxlen);
#else /* OSPFv3 */
    cli_msg(-1015, "Interface %s (IID %d)", ifa->iface->name, ifa->instance_id);
#endif
    cli_msg(-1015, "\tType: %s %s", ospf_it[ifa->type], strict);
    cli_msg(-1015, "\tArea: %R (%u)", ifa->oa->areaid, ifa->oa->areaid);
  }
  cli_msg(-1015, "\tState: %s %s", ospf_is[ifa->state],
	  ifa->stub ? "(stub)" : "");
  cli_msg(-1015, "\tPriority: %u", ifa->priority);
  cli_msg(-1015, "\tCost: %u", ifa->cost);
  if (ifa->oa->po->ecmp)
    cli_msg(-1015, "\tECMP weight: %d", ((int) ifa->ecmp_weight) + 1);
  cli_msg(-1015, "\tHello timer: %u", ifa->helloint);

  if (ifa->type == OSPF_IT_NBMA)
  {
    cli_msg(-1015, "\tPoll timer: %u", ifa->pollint);
  }
  cli_msg(-1015, "\tWait timer: %u", ifa->waitint);
  cli_msg(-1015, "\tDead timer: %u", ifa->dead);
  cli_msg(-1015, "\tRetransmit timer: %u", ifa->rxmtint);
  if ((ifa->type == OSPF_IT_BCAST) || (ifa->type == OSPF_IT_NBMA))
  {
    cli_msg(-1015, "\tDesigned router (ID): %R", ifa->drid);
    cli_msg(-1015, "\tDesigned router (IP): %I", ifa->drip);
    cli_msg(-1015, "\tBackup designed router (ID): %R", ifa->bdrid);
    cli_msg(-1015, "\tBackup designed router (IP): %I", ifa->bdrip);
  }
}

void
ospf_iface_shutdown(struct ospf_iface *ifa)
{
  init_list(&ifa->neigh_list);
  hello_timer_hook(ifa->hello_timer);
}
