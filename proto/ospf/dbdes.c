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


struct ospf_dbdes2_packet
{
  struct ospf_packet hdr;
  union ospf_auth auth;

  u16 iface_mtu;
  u8 options;
  u8 imms;			/* I, M, MS bits */
  u32 ddseq;

  struct ospf_lsa_header lsas[];
};

struct ospf_dbdes3_packet
{
  struct ospf_packet hdr;

  u32 options;
  u16 iface_mtu;
  u8 padding;
  u8 imms;			/* I, M, MS bits */
  u32 ddseq;

  struct ospf_lsa_header lsas[];
};


static inline uint
ospf_dbdes_hdrlen(struct ospf_proto *p)
{
  return ospf_is_v2(p) ?
    sizeof(struct ospf_dbdes2_packet) : sizeof(struct ospf_dbdes3_packet);
}


static void
ospf_dbdes_body(struct ospf_proto *p, struct ospf_packet *pkt,
		struct ospf_lsa_header **body, uint *count)
{
  uint plen = ntohs(pkt->length);
  uint hlen = ospf_dbdes_hdrlen(p);

  *body = ((void *) pkt) + hlen;
  *count = (plen - hlen) / sizeof(struct ospf_lsa_header);
}

static void
ospf_dump_dbdes(struct ospf_proto *p, struct ospf_packet *pkt)
{
  struct ospf_lsa_header *lsas;
  uint i, lsa_count;
  u32 pkt_ddseq;
  u16 pkt_iface_mtu;
  u8 pkt_imms;

  ASSERT(pkt->type == DBDES_P);
  ospf_dump_common(p, pkt);

  if (ospf_is_v2(p))
  {
    struct ospf_dbdes2_packet *ps = (void *) pkt;
    pkt_iface_mtu = ntohs(ps->iface_mtu);
    pkt_imms = ps->imms;
    pkt_ddseq = ntohl(ps->ddseq);
  }
  else /* OSPFv3 */
  {
    struct ospf_dbdes3_packet *ps = (void *) pkt;
    pkt_iface_mtu = ntohs(ps->iface_mtu);
    pkt_imms = ps->imms;
    pkt_ddseq = ntohl(ps->ddseq);
  }

  log(L_TRACE "%s:     mtu      %u", p->p.name, pkt_iface_mtu);
  log(L_TRACE "%s:     imms     %s%s%s", p->p.name,
      (pkt_imms & DBDES_I) ? "I " : "",
      (pkt_imms & DBDES_M) ? "M " : "",
      (pkt_imms & DBDES_MS) ? "MS" : "");
  log(L_TRACE "%s:     ddseq    %u", p->p.name, pkt_ddseq);

  ospf_dbdes_body(p, pkt, &lsas, &lsa_count);
  for (i = 0; i < lsa_count; i++)
    ospf_dump_lsahdr(p, lsas + i);
}


static void
ospf_prepare_dbdes(struct ospf_proto *p, struct ospf_neighbor *n, int body)
{
  struct ospf_iface *ifa = n->ifa;
  struct ospf_packet *pkt;
  uint length;

  u16 iface_mtu = (ifa->type == OSPF_IT_VLINK) ? 0 : ifa->iface->mtu;

  if (n->ldd_bsize != ifa->tx_length)
  {
    mb_free(n->ldd_buffer);
    n->ldd_buffer = mb_allocz(n->pool, ifa->tx_length);
    n->ldd_bsize = ifa->tx_length;
  }

  pkt = n->ldd_buffer;
  ospf_pkt_fill_hdr(ifa, pkt, DBDES_P);

  if (ospf_is_v2(p))
  {
    struct ospf_dbdes2_packet *ps = (void *) pkt;
    ps->iface_mtu = htons(iface_mtu);
    ps->options = ifa->oa->options;
    ps->imms = 0;	/* Will be set later */
    ps->ddseq = htonl(n->dds);
    length = sizeof(struct ospf_dbdes2_packet);
  }
  else /* OSPFv3 */
  {
    struct ospf_dbdes3_packet *ps = (void *) pkt;
    ps->options = htonl(ifa->oa->options);
    ps->iface_mtu = htons(iface_mtu);
    ps->padding = 0;
    ps->imms = 0;	/* Will be set later */
    ps->ddseq = htonl(n->dds);
    length = sizeof(struct ospf_dbdes3_packet);
  }

  if (body && (n->myimms & DBDES_M))
  {
    struct ospf_lsa_header *lsas;
    struct top_hash_entry *en;
    uint i = 0, lsa_max;

    ospf_dbdes_body(p, pkt, &lsas, &lsa_max);
    en = (void *) s_get(&(n->dbsi));

    while (i < lsa_max)
    {
      if (!SNODE_VALID(en))
      {
	n->myimms &= ~DBDES_M;	/* Unset More bit */
	break;
      }

      if ((en->lsa.age < LSA_MAXAGE) &&
	  lsa_flooding_allowed(en->lsa_type, en->domain, ifa))
      {
	lsa_hton_hdr(&(en->lsa), lsas + i);
	i++;
      }

      en = SNODE_NEXT(en);
    }

    s_put(&(n->dbsi), SNODE en);

    length += i * sizeof(struct ospf_lsa_header);
  }

  if (ospf_is_v2(p))
    ((struct ospf_dbdes2_packet *) pkt)->imms = n->myimms;
  else
    ((struct ospf_dbdes3_packet *) pkt)->imms = n->myimms;

  pkt->length = htons(length);
}

static void
ospf_do_send_dbdes(struct ospf_proto *p, struct ospf_neighbor *n)
{
  struct ospf_iface *ifa = n->ifa;

  OSPF_PACKET(ospf_dump_dbdes, n->ldd_buffer,
	      "DBDES packet sent to %I via %s", n->ip, ifa->ifname);
  sk_set_tbuf(ifa->sk, n->ldd_buffer);
  ospf_send_to(ifa, n->ip);
  sk_set_tbuf(ifa->sk, NULL);
}

/**
 * ospf_send_dbdes - transmit database description packet
 * @n: neighbor
 * @next: whether to send a next packet in a sequence (1) or to retransmit the old one (0)
 *
 * Sending of a database description packet is described in 10.8 of RFC 2328.
 * Reception of each packet is acknowledged in the sequence number of another.
 * When I send a packet to a neighbor I keep a copy in a buffer. If the neighbor
 * does not reply, I don't create a new packet but just send the content
 * of the buffer.
 */
void
ospf_send_dbdes(struct ospf_proto *p, struct ospf_neighbor *n, int next)
{
  /* RFC 2328 10.8 */

  if (n->ifa->oa->rt == NULL)
    return;

  switch (n->state)
  {
  case NEIGHBOR_EXSTART:
    n->myimms |= DBDES_I;

    /* Send empty packets */
    ospf_prepare_dbdes(p, n, 0);
    ospf_do_send_dbdes(p, n);
    break;

  case NEIGHBOR_EXCHANGE:
    n->myimms &= ~DBDES_I;

    if (next)
      ospf_prepare_dbdes(p, n, 1);

    /* Send prepared packet */
    ospf_do_send_dbdes(p, n);

    /* Master should restart RXMT timer for each DBDES exchange */
    if (n->myimms & DBDES_MS)
      tm_start(n->rxmt_timer, n->ifa->rxmtint);

    if (!(n->myimms & DBDES_MS))
      if (!(n->myimms & DBDES_M) &&
	  !(n->imms & DBDES_M))
	ospf_neigh_sm(n, INM_EXDONE);
    break;

  case NEIGHBOR_LOADING:
  case NEIGHBOR_FULL:

    if (!n->ldd_buffer)
    {
      OSPF_TRACE(D_PACKETS, "No DBDES packet for repeating");
      ospf_neigh_sm(n, INM_KILLNBR);
      return;
    }

    /* Send last packet */
    ospf_do_send_dbdes(p, n);
    break;
  }
}


static int
ospf_process_dbdes(struct ospf_proto *p, struct ospf_packet *pkt, struct ospf_neighbor *n)
{
  struct ospf_iface *ifa = n->ifa;
  struct ospf_lsa_header *lsas;
  uint i, lsa_count;

  ospf_dbdes_body(p, pkt, &lsas, &lsa_count);

  for (i = 0; i < lsa_count; i++)
  {
    struct top_hash_entry *en, *req;
    struct ospf_lsa_header lsa;
    u32 lsa_type, lsa_domain;

    lsa_ntoh_hdr(lsas + i, &lsa);
    lsa_get_type_domain(&lsa, ifa, &lsa_type, &lsa_domain);

    /* RFC 2328 10.6 and RFC 5340 4.2.2 */

    if (!lsa_type)
    {
      log(L_WARN "%s: Bad DBDES from %I - LSA of unknown type", p->p.name, n->ip);
      goto err;
    }

    if (!oa_is_ext(ifa->oa) && (LSA_SCOPE(lsa_type) == LSA_SCOPE_AS))
    {
      log(L_WARN "%s: Bad DBDES from %I - LSA with AS scope in stub area", p->p.name, n->ip);
      goto err;
    }

    /* Errata 3746 to RFC 2328 - rt-summary-LSAs forbidden in stub areas */
    if (!oa_is_ext(ifa->oa) && (lsa_type == LSA_T_SUM_RT))
    {
      log(L_WARN "%s: Bad DBDES from %I - rt-summary-LSA in stub area", p->p.name, n->ip);
      goto err;
    }

    /* Not explicitly mentioned in RFC 5340 4.2.2 but makes sense */
    if (LSA_SCOPE(lsa_type) == LSA_SCOPE_RES)
    {
      log(L_WARN "%s: Bad DBDES from %I - LSA with invalid scope", p->p.name, n->ip);
      goto err;
    }

    en = ospf_hash_find(p->gr, lsa_domain, lsa.id, lsa.rt, lsa_type);
    if (!en || (lsa_comp(&lsa, &(en->lsa)) == CMP_NEWER))
    {
      req = ospf_hash_get(n->lsrqh, lsa_domain, lsa.id, lsa.rt, lsa_type);

      if (!SNODE_VALID(req))
	s_add_tail(&n->lsrql, SNODE req);

      req->lsa = lsa;
      req->lsa_body = LSA_BODY_DUMMY;
    }
  }

  return 0;

 err:
  ospf_neigh_sm(n, INM_SEQMIS);
  return -1;
}

void
ospf_receive_dbdes(struct ospf_packet *pkt, struct ospf_iface *ifa,
		   struct ospf_neighbor *n)
{
  struct ospf_proto *p = ifa->oa->po;
  u32 rcv_ddseq, rcv_options;
  u16 rcv_iface_mtu;
  u8 rcv_imms;
  uint plen;

  /* RFC 2328 10.6 */

  plen = ntohs(pkt->length);
  if (plen < ospf_dbdes_hdrlen(p))
  {
    log(L_ERR "OSPF: Bad DBDES packet from %I - too short (%u B)", n->ip, plen);
    return;
  }

  OSPF_PACKET(ospf_dump_dbdes, pkt, "DBDES packet received from %I via %s", n->ip, ifa->ifname);

  ospf_neigh_sm(n, INM_HELLOREC);

  if (ospf_is_v2(p))
  {
    struct ospf_dbdes2_packet *ps = (void *) pkt;
    rcv_iface_mtu = ntohs(ps->iface_mtu);
    rcv_options = ps->options;
    rcv_imms = ps->imms;
    rcv_ddseq = ntohl(ps->ddseq);
  }
  else /* OSPFv3 */
  {
    struct ospf_dbdes3_packet *ps = (void *) pkt;
    rcv_options = ntohl(ps->options);
    rcv_iface_mtu = ntohs(ps->iface_mtu);
    rcv_imms = ps->imms;
    rcv_ddseq = ntohl(ps->ddseq);
  }

  switch (n->state)
  {
  case NEIGHBOR_DOWN:
  case NEIGHBOR_ATTEMPT:
  case NEIGHBOR_2WAY:
    return;

  case NEIGHBOR_INIT:
    ospf_neigh_sm(n, INM_2WAYREC);
    if (n->state != NEIGHBOR_EXSTART)
      return;

  case NEIGHBOR_EXSTART:
    if ((ifa->type != OSPF_IT_VLINK) &&
	(rcv_iface_mtu != ifa->iface->mtu) &&
	(rcv_iface_mtu != 0) &&
	(ifa->iface->mtu != 0))
      log(L_WARN "OSPF: MTU mismatch with neighbor %I on interface %s (remote %d, local %d)",
	  n->ip, ifa->ifname, rcv_iface_mtu, ifa->iface->mtu);

    if ((rcv_imms == DBDES_IMMS) &&
	(n->rid > p->router_id) &&
	(plen == ospf_dbdes_hdrlen(p)))
    {
      /* I'm slave! */
      n->dds = rcv_ddseq;
      n->ddr = rcv_ddseq;
      n->options = rcv_options;
      n->myimms &= ~DBDES_MS;
      n->imms = rcv_imms;
      OSPF_TRACE(D_PACKETS, "I'm slave to %I", n->ip);
      ospf_neigh_sm(n, INM_NEGDONE);
      ospf_send_dbdes(p, n, 1);
      break;
    }

    if (!(rcv_imms & DBDES_I) &&
	!(rcv_imms & DBDES_MS) &&
	(n->rid < p->router_id) &&
	(n->dds == rcv_ddseq))
    {
      /* I'm master! */
      n->options = rcv_options;
      n->ddr = rcv_ddseq - 1;	/* It will be set corectly a few lines down */
      n->imms = rcv_imms;
      OSPF_TRACE(D_PACKETS, "I'm master to %I", n->ip);
      ospf_neigh_sm(n, INM_NEGDONE);
    }
    else
    {
      DBG("%s: Nothing happend to %I (imms=%d)\n", p->name, n->ip, rcv_imms);
      break;
    }

  case NEIGHBOR_EXCHANGE:
    if ((rcv_imms == n->imms) &&
	(rcv_options == n->options) &&
	(rcv_ddseq == n->ddr))
    {
      /* Duplicate packet */
      OSPF_TRACE(D_PACKETS, "Received duplicate dbdes from %I", n->ip);
      if (!(n->myimms & DBDES_MS))
      {
	/* Slave should retransmit dbdes packet */
	ospf_send_dbdes(p, n, 0);
      }
      return;
    }

    if ((rcv_imms & DBDES_MS) != (n->imms & DBDES_MS))	/* M/S bit differs */
    {
      OSPF_TRACE(D_PACKETS, "dbdes - sequence mismatch neighbor %I (bit MS)", n->ip);
      ospf_neigh_sm(n, INM_SEQMIS);
      break;
    }

    if (rcv_imms & DBDES_I)		/* I bit is set */
    {
      OSPF_TRACE(D_PACKETS, "dbdes - sequence mismatch neighbor %I (bit I)", n->ip);
      ospf_neigh_sm(n, INM_SEQMIS);
      break;
    }

    if (rcv_options != n->options)	/* Options differs */
    {
      OSPF_TRACE(D_PACKETS, "dbdes - sequence mismatch neighbor %I (options)", n->ip);
      ospf_neigh_sm(n, INM_SEQMIS);
      break;
    }

    n->ddr = rcv_ddseq;
    n->imms = rcv_imms;

    if (n->myimms & DBDES_MS)
    {
      if (rcv_ddseq != n->dds)	/* MASTER */
      {
	OSPF_TRACE(D_PACKETS, "dbdes - sequence mismatch neighbor %I (master)", n->ip);
	ospf_neigh_sm(n, INM_SEQMIS);
	break;
      }

      n->dds++;

      if (ospf_process_dbdes(p, pkt, n) < 0)
	return;

      if (!(n->myimms & DBDES_M) && !(n->imms & DBDES_M))
	ospf_neigh_sm(n, INM_EXDONE);
      else
	ospf_send_dbdes(p, n, 1);
    }
    else
    {
      if (rcv_ddseq != (n->dds + 1))	/* SLAVE */
      {
	OSPF_TRACE(D_PACKETS, "dbdes - sequence mismatch neighbor %I (slave)", n->ip);
	ospf_neigh_sm(n, INM_SEQMIS);
	break;
      }

      n->ddr = rcv_ddseq;
      n->dds = rcv_ddseq;

      if (ospf_process_dbdes(p, pkt, n) < 0)
	return;

      ospf_send_dbdes(p, n, 1);
    }
    break;

  case NEIGHBOR_LOADING:
  case NEIGHBOR_FULL:
    if ((rcv_imms == n->imms) &&
	(rcv_options == n->options) &&
	(rcv_ddseq == n->ddr))
    {
      /* Duplicate packet */
      OSPF_TRACE(D_PACKETS, "Received duplicate dbdes from %I", n->ip);
      if (!(n->myimms & DBDES_MS))
      {
	/* Slave should retransmit dbdes packet */
	ospf_send_dbdes(p, n, 0);
      }
      return;
    }
    else
    {
      OSPF_TRACE(D_PACKETS, "dbdes - sequence mismatch neighbor %I (full)", n->ip);
      DBG("PS=%u, DDR=%u, DDS=%u\n", rcv_ddseq, n->ddr, n->dds);
      ospf_neigh_sm(n, INM_SEQMIS);
    }
    break;

  default:
    bug("Received dbdes from %I in undefined state.", n->ip);
  }
}
