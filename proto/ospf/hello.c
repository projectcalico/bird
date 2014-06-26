/*
 *	BIRD -- OSPF
 *
 *	(c) 1999--2004 Ondrej Filip <feela@network.cz>
 *	(c) 2009--2014 Ondrej Zajicek <santiago@crfreenet.org>
 *	(c) 2009--2014 CZ.NIC z.s.p.o.
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"


struct ospf_hello2_packet
{
  struct ospf_packet hdr;
  union ospf_auth auth;

  u32 netmask;
  u16 helloint;
  u8 options;
  u8 priority;
  u32 deadint;
  u32 dr;
  u32 bdr;

  u32 neighbors[];
};

struct ospf_hello3_packet
{
  struct ospf_packet hdr;

  u32 iface_id;
  u8 priority;
  u8 options3;
  u8 options2;
  u8 options;
  u16 helloint;
  u16 deadint;
  u32 dr;
  u32 bdr;

  u32 neighbors[];
};


void
ospf_send_hello(struct ospf_iface *ifa, int kind, struct ospf_neighbor *dirn)
{
  struct ospf_proto *p = ifa->oa->po;
  struct ospf_packet *pkt;
  struct ospf_neighbor *neigh, *n1;
  struct nbma_node *nb;
  u32 *neighbors;
  uint length;
  int i, max;

  if (ifa->state <= OSPF_IS_LOOP)
    return;

  if (ifa->stub)
    return;


  pkt = ospf_tx_buffer(ifa);
  ospf_pkt_fill_hdr(ifa, pkt, HELLO_P);

  if (ospf_is_v2(p))
  {
    struct ospf_hello2_packet *ps = (void *) pkt;

    if ((ifa->type == OSPF_IT_VLINK) ||
	((ifa->type == OSPF_IT_PTP) && !ifa->ptp_netmask))
      ps->netmask = 0;
    else
      ps->netmask = htonl(u32_mkmask(ifa->addr->pxlen));

    ps->helloint = ntohs(ifa->helloint);
    ps->options = ifa->oa->options;
    ps->priority = ifa->priority;
    ps->deadint = htonl(ifa->deadint);
    ps->dr = htonl(ipa_to_u32(ifa->drip));
    ps->bdr = htonl(ipa_to_u32(ifa->bdrip));

    length = sizeof(struct ospf_hello2_packet);
    neighbors = ps->neighbors;
  }
  else
  {
    struct ospf_hello3_packet *ps = (void *) pkt;

    ps->iface_id = htonl(ifa->iface_id);
    ps->priority = ifa->priority;
    ps->options3 = ifa->oa->options >> 16;
    ps->options2 = ifa->oa->options >> 8;
    ps->options = ifa->oa->options;
    ps->helloint = ntohs(ifa->helloint);
    ps->deadint = htons(ifa->deadint);
    ps->dr = htonl(ifa->drid);
    ps->bdr = htonl(ifa->bdrid);

    length = sizeof(struct ospf_hello3_packet);
    neighbors = ps->neighbors;
  }

  i = 0;
  max = (ospf_pkt_maxsize(ifa) - length) / sizeof(u32);

  /* Fill all neighbors */
  if (kind != OHS_SHUTDOWN)
  {
    WALK_LIST(neigh, ifa->neigh_list)
    {
      if (i == max)
      {
 	log(L_WARN "%s: Too many neighbors on interface %s", p->p.name, ifa->ifname);
	break;
      }
      neighbors[i] = htonl(neigh->rid);
      i++;
    }
  }

  length += i * sizeof(u32);
  pkt->length = htons(length);

  OSPF_TRACE(D_PACKETS, "HELLO packet sent via %s", ifa->ifname);

  switch(ifa->type)
  {
  case OSPF_IT_BCAST:
  case OSPF_IT_PTP:
    ospf_send_to_all(ifa);
    break;

  case OSPF_IT_NBMA:
    if (dirn)		/* Response to received hello */
    {
      ospf_send_to(ifa, dirn->ip);
      break;
    }

    int to_all = ifa->state > OSPF_IS_DROTHER;
    int me_elig = ifa->priority > 0;
 
    if (kind == OHS_POLL)	/* Poll timer */
    {
      WALK_LIST(nb, ifa->nbma_list)
	if (!nb->found && (to_all || (me_elig && nb->eligible)))
	  ospf_send_to(ifa, nb->ip);
    }
    else			/* Hello timer */
    {
      WALK_LIST(n1, ifa->neigh_list)
	if (to_all || (me_elig && (n1->priority > 0)) ||
	    (n1->rid == ifa->drid) || (n1->rid == ifa->bdrid))
	  ospf_send_to(ifa, n1->ip);
    }
    break;

  case OSPF_IT_PTMP:
    WALK_LIST(n1, ifa->neigh_list)
      ospf_send_to(ifa, n1->ip);

    WALK_LIST(nb, ifa->nbma_list)
      if (!nb->found)
	ospf_send_to(ifa, nb->ip);

    /* If there is no other target, we also send HELLO packet to the other end */
    if (ipa_nonzero(ifa->addr->opposite) && !ifa->strictnbma &&
	EMPTY_LIST(ifa->neigh_list) && EMPTY_LIST(ifa->nbma_list))
      ospf_send_to(ifa, ifa->addr->opposite);
    break;

  case OSPF_IT_VLINK:
    ospf_send_to(ifa, ifa->vip);
    break;

  default:
    bug("Bug in ospf_send_hello()");
  }
}


void
ospf_receive_hello(struct ospf_packet *pkt, struct ospf_iface *ifa,
		   struct ospf_neighbor *n, ip_addr faddr)
{
  struct ospf_proto *p = ifa->oa->po;
  char *beg = "OSPF: Bad HELLO packet from ";
  u32 rcv_iface_id, rcv_helloint, rcv_deadint, rcv_dr, rcv_bdr;
  u8 rcv_options, rcv_priority;
  u32 *neighbors;
  u32 neigh_count;
  uint plen, i;

  /* RFC 2328 10.5 */

  OSPF_TRACE(D_PACKETS, "HELLO packet received from %I via %s", faddr, ifa->ifname);

  plen = ntohs(pkt->length);

  if (ospf_is_v2(p))
  {
    struct ospf_hello2_packet *ps = (void *) pkt;

    if (plen < sizeof(struct ospf_hello2_packet))
    {
      log(L_ERR "%s%I - too short (%u B)", beg, faddr, plen);
      return;
    }

    rcv_iface_id = 0;
    rcv_helloint = ntohs(ps->helloint);
    rcv_deadint = ntohl(ps->deadint);
    rcv_dr = ntohl(ps->dr);
    rcv_bdr = ntohl(ps->bdr);
    rcv_options = ps->options;
    rcv_priority = ps->priority;

    int pxlen = u32_masklen(ntohl(ps->netmask));
    if ((ifa->type != OSPF_IT_VLINK) &&
	(ifa->type != OSPF_IT_PTP) &&
	(pxlen != ifa->addr->pxlen))
    {
      log(L_ERR "%s%I - prefix length mismatch (%d)", beg, faddr, pxlen);
      return;
    }

    neighbors = ps->neighbors;
    neigh_count = (plen - sizeof(struct ospf_hello2_packet)) / sizeof(u32);
  }
  else /* OSPFv3 */
  {
    struct ospf_hello3_packet *ps = (void *) pkt;

    if (plen < sizeof(struct ospf_hello3_packet))
    {
      log(L_ERR "%s%I - too short (%u B)", beg, faddr, plen);
      return;
    }

    rcv_iface_id = ntohl(ps->iface_id);
    rcv_helloint = ntohs(ps->helloint);
    rcv_deadint = ntohs(ps->deadint);
    rcv_dr = ntohl(ps->dr);
    rcv_bdr = ntohl(ps->bdr);
    rcv_options = ps->options;
    rcv_priority = ps->priority;

    neighbors = ps->neighbors;
    neigh_count = (plen - sizeof(struct ospf_hello3_packet)) / sizeof(u32);
  }

  if (rcv_helloint != ifa->helloint)
  {
    log(L_ERR "%s%I - hello interval mismatch (%d)", beg, faddr, rcv_helloint);
    return;
  }

  if (rcv_deadint != ifa->deadint)
  {
    log(L_ERR "%s%I - dead interval mismatch (%d)", beg, faddr, rcv_deadint);
    return;
  }

  /* Check whether bits E, N match */
  if ((rcv_options ^ ifa->oa->options) & (OPT_E | OPT_N))
  {
    log(L_ERR "%s%I - area type mismatch (%x)", beg, faddr, rcv_options);
    return;
  }

  /* Check consistency of existing neighbor entry */
  if (n)
  {
    unsigned t = ifa->type;
    if (ospf_is_v2(p) && ((t == OSPF_IT_BCAST) || (t == OSPF_IT_NBMA) || (t == OSPF_IT_PTMP)))
    {
      /* Neighbor identified by IP address; Router ID may change */
      if (n->rid != ntohl(pkt->routerid))
      {
	OSPF_TRACE(D_EVENTS, "Neighbor %I has changed Router ID from %R to %R",
		   n->ip, n->rid, ntohl(pkt->routerid));
	ospf_neigh_remove(n);
	n = NULL;
      }
    }
    else /* OSPFv3 or OSPFv2/PtP */
    {
      /* Neighbor identified by Router ID; IP address may change */
      if (!ipa_equal(faddr, n->ip))
      {
	OSPF_TRACE(D_EVENTS, "Neighbor address changed from %I to %I", n->ip, faddr);
	n->ip = faddr;
      }
    }
  }

  if (!n)
  {
    if ((ifa->type == OSPF_IT_NBMA) || (ifa->type == OSPF_IT_PTMP))
    {
      struct nbma_node *nn = find_nbma_node(ifa, faddr);

      if (!nn && ifa->strictnbma)
      {
	log(L_WARN "Ignoring new neighbor: %I on %s", faddr, ifa->ifname);
	return;
      }

      if (nn && (ifa->type == OSPF_IT_NBMA) &&
	  (((rcv_priority == 0) && nn->eligible) ||
	   ((rcv_priority > 0) && !nn->eligible)))
      {
	log(L_ERR "Eligibility mismatch for neighbor: %I on %s", faddr, ifa->ifname);
	return;
      }

      if (nn)
	nn->found = 1;
    }

    OSPF_TRACE(D_EVENTS, "New neighbor found: %I on %s", faddr, ifa->ifname);

    n = ospf_neighbor_new(ifa);

    n->rid = ntohl(pkt->routerid);
    n->ip = faddr;
    n->dr = rcv_dr;
    n->bdr = rcv_bdr;
    n->priority = rcv_priority;
    n->iface_id = rcv_iface_id;

    if (n->ifa->cf->bfd)
      ospf_neigh_update_bfd(n, n->ifa->bfd);
  }

  u32 n_id = ospf_is_v2(p) ? ipa_to_u32(n->ip) : n->rid;

  u32 old_dr = n->dr;
  u32 old_bdr = n->bdr;
  u32 old_priority = n->priority;
  u32 old_iface_id = n->iface_id;

  n->dr = rcv_dr;
  n->bdr = rcv_bdr;
  n->priority = rcv_priority;
  n->iface_id = rcv_iface_id;


  /* Update inactivity timer */
  ospf_neigh_sm(n, INM_HELLOREC);

  /* RFC 2328 9.5.1 - non-eligible routers reply to hello on NBMA nets */
  if (ifa->type == OSPF_IT_NBMA)
    if ((ifa->priority == 0) && (n->priority > 0))
      ospf_send_hello(n->ifa, OHS_HELLO, n);
 

  /* Examine list of neighbors */
  for (i = 0; i < neigh_count; i++)
    if (neighbors[i] == htonl(p->router_id))
      goto found_self;

  ospf_neigh_sm(n, INM_1WAYREC);
  return;

 found_self:
  ospf_neigh_sm(n, INM_2WAYREC);


  if (n->iface_id != old_iface_id)
  {
    /* If neighbor is DR, also update cached DR interface ID */
    if (ifa->drid == n->rid)
      ifa->dr_iface_id = n->iface_id;

    /* RFC 5340 4.4.3 Event 4 - change of neighbor's interface ID */
    ospf_notify_rt_lsa(ifa->oa);

    /* Missed in RFC 5340 4.4.3 Event 4 - (Px-)Net-LSA uses iface_id to ref Link-LSAs */
    ospf_notify_net_lsa(ifa);
  }

  if (ifa->state == OSPF_IS_WAITING)
  {
    /* Neighbor is declaring itself DR (and there is no BDR) or as BDR */
    if (((n->dr == n_id) && (n->bdr == 0)) || (n->bdr == n_id))
      ospf_iface_sm(ifa, ISM_BACKS);
  }
  else if (ifa->state >= OSPF_IS_DROTHER)
  {
    /* Neighbor changed priority or started/stopped declaring itself as DR/BDR */
    if ((n->priority != old_priority) ||
	((n->dr == n_id) && (old_dr != n_id)) ||
	((n->dr != n_id) && (old_dr == n_id)) ||
	((n->bdr == n_id) && (old_bdr != n_id)) ||
	((n->bdr != n_id) && (old_bdr == n_id)))
      ospf_iface_sm(ifa, ISM_NEICH);
  }
}
