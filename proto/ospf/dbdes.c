/*
 *	BIRD -- OSPF
 *
 *	(c) 1999--2004 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

/**
 * ospf_dbdes_send - transmit database description packet
 * @n: neighbor
 *
 * Sending of a database description packet is described in 10.6 of RFC 2328.
 * Reception of each packet is acknowledged in the sequence number of another.
 * When I send a packet to a neighbor I keep a copy in a buffer. If the neighbor
 * does not reply, I don't create a new packet but just send the content
 * of the buffer.
 */
void
ospf_dbdes_send(struct ospf_neighbor *n)
{
  struct ospf_dbdes_packet *pkt;
  struct ospf_packet *op;
  struct ospf_iface *ifa = n->ifa;
  struct ospf_area *oa = ifa->oa;
  u16 length;
  struct proto *p = (struct proto *) (ifa->proto);
  u16 i, j;

  if ((oa->rt == NULL) || (EMPTY_LIST(oa->lsal)))
    originate_rt_lsa(oa);

  switch (n->state)
  {
  case NEIGHBOR_EXSTART:	/* Send empty packets */
    n->myimms.bit.i = 1;
    pkt = (struct ospf_dbdes_packet *) (ifa->ip_sk->tbuf);
    op = (struct ospf_packet *) pkt;
    fill_ospf_pkt_hdr(ifa, pkt, DBDES_P);
    pkt->iface_mtu = htons(ifa->iface->mtu);	/* FIXME NOT for VLINK! */
    pkt->options = ifa->options;
    pkt->imms = n->myimms;
    pkt->ddseq = htonl(n->dds);
    length = sizeof(struct ospf_dbdes_packet);
    op->length = htons(length);
    ospf_pkt_finalize(ifa, op);
    sk_send_to(ifa->ip_sk, length, n->ip, OSPF_PROTO);
    OSPF_TRACE(D_PACKETS, "DB_DES (I) sent to %I via %s.", n->ip,
	       ifa->iface->name);
    break;

  case NEIGHBOR_EXCHANGE:
    n->myimms.bit.i = 0;

    if (((n->myimms.bit.ms) && (n->dds == n->ddr + 1)) ||
	((!(n->myimms.bit.ms)) && (n->dds == n->ddr)))
    {
      snode *sn;		/* Send next */
      struct ospf_lsa_header *lsa;

      pkt = n->ldbdes;
      op = (struct ospf_packet *) pkt;

      fill_ospf_pkt_hdr(ifa, pkt, DBDES_P);
      pkt->iface_mtu = htons(ifa->iface->mtu);
      pkt->options = ifa->options;
      pkt->ddseq = htonl(n->dds);

      j = i = (ifa->iface->mtu - sizeof(struct ospf_dbdes_packet) - SIPH) / sizeof(struct ospf_lsa_header);	/* Number of possible lsaheaders to send */
      lsa = (n->ldbdes + sizeof(struct ospf_dbdes_packet));

      if (n->myimms.bit.m)
      {
	sn = s_get(&(n->dbsi));

	DBG("Number of LSA: %d\n", j);
	for (; i > 0; i--)
	{
	  struct top_hash_entry *en;

	  en = (struct top_hash_entry *) sn;
	  htonlsah(&(en->lsa), lsa);
	  DBG("Working on: %d\n", i);
	  DBG("\tX%01x %-1I %-1I %p\n", en->lsa.type, en->lsa.id,
	      en->lsa.rt, en->lsa_body);

	  if (sn == STAIL(n->ifa->oa->lsal))
	  {
	    i--;
	    break;		/* Should set some flag? */
	  }
	  sn = sn->next;
	  lsa++;
	}

	if (sn == STAIL(n->ifa->oa->lsal))
	{
	  DBG("Number of LSA NOT sent: %d\n", i);
	  DBG("M bit unset.\n");
	  n->myimms.bit.m = 0;	/* Unset more bit */
	}
	else
	  s_put(&(n->dbsi), sn);
      }

      pkt->imms.byte = n->myimms.byte;

      length = (j - i) * sizeof(struct ospf_lsa_header) +
	sizeof(struct ospf_dbdes_packet);
      op->length = htons(length);

      ospf_pkt_finalize(ifa, op);
      DBG("%s: DB_DES (M) prepared for %I.\n", p->name, n->ip);
    }

  case NEIGHBOR_LOADING:
  case NEIGHBOR_FULL:
    length = ntohs(((struct ospf_packet *) n->ldbdes)->length);

    if (!length)
    {
      OSPF_TRACE(D_PACKETS, "No packet in my buffer for repeating");
      ospf_neigh_sm(n, INM_KILLNBR);
      return;
    }

    memcpy(ifa->ip_sk->tbuf, n->ldbdes, length);
    /* Copy last sent packet again */

    sk_send_to(ifa->ip_sk, length, n->ip, OSPF_PROTO);
    OSPF_TRACE(D_PACKETS, "DB_DES (M) sent to %I via %s.", n->ip,
	       ifa->iface->name);
    if (!n->myimms.bit.ms)
    {
      if ((n->myimms.bit.m == 0) && (n->imms.bit.m == 0) &&
	  (n->state == NEIGHBOR_EXCHANGE))
      {
	ospf_neigh_sm(n, INM_EXDONE);
      }
    }
    break;

  default:			/* Ignore it */
    break;
  }
}

static void
ospf_dbdes_reqladd(struct ospf_dbdes_packet *ps, struct ospf_neighbor *n)
{
  struct ospf_lsa_header *plsa, lsa;
  struct top_hash_entry *he, *sn;
  struct top_graph *gr = n->ifa->oa->gr;
  struct ospf_packet *op;
  int i, j;

  op = (struct ospf_packet *) ps;

  plsa = (void *) (ps + 1);

  j = (ntohs(op->length) - sizeof(struct ospf_dbdes_packet)) /
    sizeof(struct ospf_lsa_header);

  for (i = 0; i < j; i++)
  {
    ntohlsah(plsa + i, &lsa);
    if (((he = ospf_hash_find(gr, lsa.id, lsa.rt, lsa.type)) == NULL) ||
	(lsa_comp(&lsa, &(he->lsa)) == 1))
    {
      /* Is this condition necessary? */
      if (ospf_hash_find(n->lsrqh, lsa.id, lsa.rt, lsa.type) == NULL)
      {
	sn = ospf_hash_get(n->lsrqh, lsa.id, lsa.rt, lsa.type);
	ntohlsah(plsa + i, &(sn->lsa));
	s_add_tail(&(n->lsrql), SNODE sn);
      }
    }
  }
}

void
ospf_dbdes_receive(struct ospf_dbdes_packet *ps,
		   struct ospf_iface *ifa, u16 size)
{
  struct proto *p = (struct proto *) ifa->proto;
  u32 nrid, myrid = p->cf->global->router_id;
  struct ospf_neighbor *n;

  nrid = ntohl(((struct ospf_packet *) ps)->routerid);


  if ((n = find_neigh(ifa, nrid)) == NULL)
  {
    OSPF_TRACE(D_PACKETS, "Received dbdes from unknown neigbor! %I.", nrid);
    return;
  }

  if (ifa->iface->mtu < size)
  {
    OSPF_TRACE(D_PACKETS, "Received dbdes larger than MTU from %I!", n->ip);
    return;
  }

  OSPF_TRACE(D_PACKETS, "Received dbdes from %I via %s.", n->ip,
	     ifa->iface->name);
  ospf_neigh_sm(n, INM_HELLOREC);

  switch (n->state)
  {
  case NEIGHBOR_DOWN:
  case NEIGHBOR_ATTEMPT:
  case NEIGHBOR_2WAY:
    return;
    break;
  case NEIGHBOR_INIT:
    ospf_neigh_sm(n, INM_2WAYREC);
    if (n->state != NEIGHBOR_EXSTART)
      return;
  case NEIGHBOR_EXSTART:
    if ((ps->imms.bit.m && ps->imms.bit.ms && ps->imms.bit.i)
	&& (n->rid > myrid) && (size == sizeof(struct ospf_dbdes_packet)))
    {
      /* I'm slave! */
      n->dds = ntohl(ps->ddseq);
      n->ddr = ntohl(ps->ddseq);
      n->options = ps->options;
      n->myimms.bit.ms = 0;
      n->imms.byte = ps->imms.byte;
      OSPF_TRACE(D_PACKETS, "I'm slave to %I.", n->ip);
      ospf_neigh_sm(n, INM_NEGDONE);
      ospf_dbdes_send(n);
      break;
    }
    else
    {
      if (((ps->imms.bit.i == 0) && (ps->imms.bit.ms == 0)) &&
	  (n->rid < myrid) && (n->dds == ntohl(ps->ddseq)))
      {
	/* I'm master! */
	n->options = ps->options;
	n->ddr = ntohl(ps->ddseq) - 1;
	n->imms.byte = ps->imms.byte;
	OSPF_TRACE(D_PACKETS, "I'm master to %I.", n->ip);
	ospf_neigh_sm(n, INM_NEGDONE);
      }
      else
      {
	DBG("%s: Nothing happend to %I (imms=%u)\n", p->name, n->ip,
	    ps->imms.byte);
	break;
      }
    }
    if (ps->imms.bit.i)
      break;
  case NEIGHBOR_EXCHANGE:
    if ((ps->imms.byte == n->imms.byte) && (ps->options == n->options) &&
	(ntohl(ps->ddseq) == n->ddr))
    {
      /* Duplicate packet */
      OSPF_TRACE(D_PACKETS, "Received duplicate dbdes from %I.", n->ip);
      if (n->imms.bit.ms == 0)
      {
	ospf_dbdes_send(n);
      }
      return;
    }

    n->ddr = ntohl(ps->ddseq);

    if (ps->imms.bit.ms != n->imms.bit.ms)	/* M/S bit differs */
    {
      OSPF_TRACE(D_PACKETS, "dbdes - sequence mismatch neighbor %I (bit MS)",
		 n->ip);
      ospf_neigh_sm(n, INM_SEQMIS);
      break;
    }

    if (ps->imms.bit.i)		/* I bit is set */
    {
      OSPF_TRACE(D_PACKETS, "dbdes - sequence mismatch neighbor %I (bit I)",
		 n->ip);
      ospf_neigh_sm(n, INM_SEQMIS);
      break;
    }

    n->imms.byte = ps->imms.byte;

    if (ps->options != n->options)	/* Options differs */
    {
      OSPF_TRACE(D_PACKETS, "dbdes - sequence mismatch neighbor %I (options)",
		 n->ip);
      ospf_neigh_sm(n, INM_SEQMIS);
      break;
    }

    if (n->myimms.bit.ms)
    {
      if (ntohl(ps->ddseq) != n->dds)	/* MASTER */
      {
	OSPF_TRACE(D_PACKETS,
		   "dbdes - sequence mismatch neighbor %I (master)", n->ip);
	ospf_neigh_sm(n, INM_SEQMIS);
	break;
      }
      n->dds++;
      DBG("Incrementing dds\n");
      ospf_dbdes_reqladd(ps, n);
      if ((n->myimms.bit.m == 0) && (ps->imms.bit.m == 0))
      {
	ospf_neigh_sm(n, INM_EXDONE);
      }
      else
      {
	ospf_dbdes_send(n);
      }

    }
    else
    {
      if (ntohl(ps->ddseq) != (n->dds + 1))	/* SLAVE */
      {
	OSPF_TRACE(D_PACKETS, "dbdes - sequence mismatch neighbor %I (slave)",
		   n->ip);
	ospf_neigh_sm(n, INM_SEQMIS);
	break;
      }
      n->ddr = ntohl(ps->ddseq);
      n->dds = ntohl(ps->ddseq);
      ospf_dbdes_reqladd(ps, n);
      ospf_dbdes_send(n);
    }

    break;
  case NEIGHBOR_LOADING:
  case NEIGHBOR_FULL:
    if ((ps->imms.byte == n->imms.byte) && (ps->options == n->options)
	&& (ntohl(ps->ddseq) == n->ddr))
      /* Only duplicate are accepted */
    {
      OSPF_TRACE(D_PACKETS, "Received duplicate dbdes from %I.", n->ip);
      return;
    }
    else
    {
      OSPF_TRACE(D_PACKETS, "dbdes - sequence mismatch neighbor %I (full)",
		 n->ip);
      ospf_neigh_sm(n, INM_SEQMIS);
    }
    break;
  default:
    bug("Received dbdes from %I in undefined state.", n->ip);
  }
}
