/*
 *	BIRD -- OSPF
 *
 *	(c) 2000--2004 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

/**
 * ospf_lsupd_flood - send received or generated lsa to the neighbors
 * @n: neighbor than sent this lsa (or NULL if generated)
 * @hn: LSA header followed by lsa body in network endianity (may be NULL) 
 * @hh: LSA header in host endianity (must be filled)
 * @po: actual instance of OSPF protocol
 * @iff: interface which received this LSA (or NULL if LSA is generated)
 * @oa: ospf_area which is the LSA generated for
 * @rtl: add this LSA into retransmission list
 *
 * return value - was the LSA flooded back?
 */

int
ospf_lsupd_flood(struct ospf_neighbor *n, struct ospf_lsa_header *hn,
		 struct ospf_lsa_header *hh, struct ospf_iface *iff,
		 struct ospf_area *oa, int rtl)
{
  struct ospf_iface *ifa;
  struct ospf_neighbor *nn;
  struct top_hash_entry *en;
  struct proto_ospf *po = oa->po;
  struct proto *p = &po->proto;
  int ret, retval = 0;

  /* pg 148 */
  WALK_LIST(NODE ifa, po->iface_list)
  {
    if (ifa->stub)
      continue;

    if (hh->type == LSA_T_EXT)
    {
      if (ifa->type == OSPF_IT_VLINK)
	continue;
      if (ifa->oa->stub)
	continue;
    }
    else
    {
      if (oa->areaid == BACKBONE)
      {
	if ((ifa->type != OSPF_IT_VLINK) && (ifa->oa != oa))
	  continue;
      }
      else
      {
	if (ifa->oa != oa)
	  continue;
      }
    }
    ret = 0;
    WALK_LIST(NODE nn, ifa->neigh_list)
    {
      if (nn->state < NEIGHBOR_EXCHANGE)
	continue;
      if (nn->state < NEIGHBOR_FULL)
      {
	if ((en = ospf_hash_find_header(nn->lsrqh, hh)) != NULL)
	{
	  switch (lsa_comp(hh, &en->lsa))
	  {
	  case CMP_OLDER:
	    continue;
	    break;
	  case CMP_SAME:
	    s_rem_node(SNODE en);
	    if (en->lsa_body != NULL)
	      mb_free(en->lsa_body);
	    en->lsa_body = NULL;
	    DBG("Removing from lsreq list for neigh %I\n", nn->rid);
	    ospf_hash_delete(nn->lsrqh, en);
	    if (EMPTY_SLIST(nn->lsrql))
	      ospf_neigh_sm(nn, INM_LOADDONE);
	    continue;
	    break;
	  case CMP_NEWER:
	    s_rem_node(SNODE en);
	    if (en->lsa_body != NULL)
	      mb_free(en->lsa_body);
	    en->lsa_body = NULL;
	    DBG("Removing from lsreq list for neigh %I\n", nn->rid);
	    ospf_hash_delete(nn->lsrqh, en);
	    if (EMPTY_SLIST(nn->lsrql))
	      ospf_neigh_sm(nn, INM_LOADDONE);
	    break;
	  default:
	    bug("Bug in lsa_comp?");
	  }
	}
      }

      if (nn == n)
	continue;

      if (rtl)
      {
	if ((en = ospf_hash_find_header(nn->lsrth, hh)) == NULL)
	{
	  en = ospf_hash_get_header(nn->lsrth, hh);
	}
	else
	{
	  s_rem_node(SNODE en);
	}
	s_add_tail(&nn->lsrtl, SNODE en);
	memcpy(&en->lsa, hh, sizeof(struct ospf_lsa_header));
	DBG("Adding LSA lsrt RT: %I, Id: %I, Type: %u for n: %I\n",
	    en->lsa.rt, en->lsa.id, en->lsa.type, nn->ip);
      }
      else
      {
	if ((en = ospf_hash_find_header(nn->lsrth, hh)) != NULL)
	{
	  s_rem_node(SNODE en);
	  if (en->lsa_body != NULL)
	    mb_free(en->lsa_body);
	  en->lsa_body = NULL;
	  ospf_hash_delete(nn->lsrth, en);
	}
      }

      ret = 1;
    }

    if (ret == 0)
      continue;			/* pg 150 (2) */

    if (ifa == iff)
    {
      if ((n->rid == iff->drid) || n->rid == iff->bdrid)
	continue;		/* pg 150 (3) */
      if (iff->state == OSPF_IS_BACKUP)
	continue;		/* pg 150 (4) */
      retval = 1;
    }

    {
      sock *sk;
      u16 len, age;
      struct ospf_lsupd_packet *pk;
      struct ospf_packet *op;
      struct ospf_lsa_header *lh;

      if (ifa->type == OSPF_IT_NBMA)
	sk = ifa->ip_sk;
      else
	sk = ifa->hello_sk;	/* FIXME is this true for PTP? */

      pk = (struct ospf_lsupd_packet *) sk->tbuf;
      op = (struct ospf_packet *) sk->tbuf;

      fill_ospf_pkt_hdr(ifa, pk, LSUPD_P);
      pk->lsano = htonl(1);

      lh = (struct ospf_lsa_header *) (pk + 1);

      /* Copy LSA into the packet */
      if (hn)
      {
	memcpy(lh, hn, ntohs(hn->length));
      }
      else
      {
	u8 *help;
	struct top_hash_entry *en;

	htonlsah(hh, lh);
	help = (u8 *) (lh + 1);
	en = ospf_hash_find_header(oa->gr, hh);
	htonlsab(en->lsa_body, help, hh->type, hh->length
		 - sizeof(struct ospf_lsa_header));
      }

      len = sizeof(struct ospf_lsupd_packet) + ntohs(lh->length);

      age = ntohs(lh->age);
      age += ifa->inftransdelay;
      if (age > LSA_MAXAGE)
	age = LSA_MAXAGE;
      lh->age = htons(age);

      op->length = htons(len);
      ospf_pkt_finalize(ifa, op);
      OSPF_TRACE(D_PACKETS, "LS upd flooded via %s", ifa->iface->name);

      if (ifa->type == OSPF_IT_NBMA)
      {
	if ((ifa->state == OSPF_IS_BACKUP) || (ifa->state == OSPF_IS_DR))
	  sk_send_to_agt(sk, len, ifa, NEIGHBOR_EXCHANGE);
	else
	  sk_send_to_bdr(sk, len, ifa);
      }
      else
      {
	if ((ifa->state == OSPF_IS_BACKUP) || (ifa->state == OSPF_IS_DR) ||
	    (ifa->type == OSPF_IT_PTP))
	  sk_send_to(sk, len, AllSPFRouters, OSPF_PROTO);
	else
	  sk_send_to(sk, len, AllDRouters, OSPF_PROTO);
      }
    }
  }
  return retval;
}

void				/* I send all I received in LSREQ */
ospf_lsupd_send_list(struct ospf_neighbor *n, list * l)
{
  struct l_lsr_head *llsh;
  u16 len;
  u32 lsano;
  struct top_hash_entry *en;
  struct ospf_lsupd_packet *pk;
  struct ospf_packet *op;
  struct proto *p = &n->ifa->oa->po->proto;
  void *pktpos;

  if (EMPTY_LIST(*l))
    return;

  pk = (struct ospf_lsupd_packet *) n->ifa->ip_sk->tbuf;
  op = (struct ospf_packet *) n->ifa->ip_sk->tbuf;

  DBG("LSupd: 1st packet\n");

  fill_ospf_pkt_hdr(n->ifa, pk, LSUPD_P);
  len = SIPH + sizeof(struct ospf_lsupd_packet);
  lsano = 0;
  pktpos = (pk + 1);

  WALK_LIST(llsh, *l)
  {
    if ((en = ospf_hash_find(n->ifa->oa->gr, llsh->lsh.id, llsh->lsh.rt,
			     llsh->lsh.type)) == NULL)
      continue;			/* Probably flushed LSA */

    DBG("Sending ID=%I, Type=%u, RT=%I Sn: 0x%x Age: %u\n",
	llsh->lsh.id, llsh->lsh.type, llsh->lsh.rt, en->lsa.sn, en->lsa.age);
    if (((u32) (len + en->lsa.length)) > n->ifa->iface->mtu)
    {
      pk->lsano = htonl(lsano);
      op->length = htons(len - SIPH);
      ospf_pkt_finalize(n->ifa, op);

      sk_send_to(n->ifa->ip_sk, len - SIPH, n->ip, OSPF_PROTO);
      OSPF_TRACE(D_PACKETS, "LS upd sent to %I (%d LSAs)", n->ip, lsano);

      DBG("LSupd: next packet\n");
      fill_ospf_pkt_hdr(n->ifa, pk, LSUPD_P);
      len = SIPH + sizeof(struct ospf_lsupd_packet);
      lsano = 0;
      pktpos = (pk + 1);
    }
    htonlsah(&(en->lsa), pktpos);
    pktpos = pktpos + sizeof(struct ospf_lsa_header);
    htonlsab(en->lsa_body, pktpos, en->lsa.type, en->lsa.length
	     - sizeof(struct ospf_lsa_header));
    pktpos = pktpos + en->lsa.length - sizeof(struct ospf_lsa_header);
    len += en->lsa.length;
    lsano++;
  }
  if (lsano > 0)
  {
    pk->lsano = htonl(lsano);
    op->length = htons(len - SIPH);
    ospf_pkt_finalize(n->ifa, op);

    OSPF_TRACE(D_PACKETS, "LS upd sent to %I (%d LSAs)", n->ip, lsano);
    sk_send_to(n->ifa->ip_sk, len - SIPH, n->ip, OSPF_PROTO);
  }
}

void
ospf_lsupd_receive(struct ospf_lsupd_packet *ps,
		   struct ospf_iface *ifa, u16 size)
{
  u32 area, nrid;
  struct ospf_neighbor *n, *ntmp;
  struct ospf_lsa_header *lsa;
  struct ospf_area *oa;
  struct proto_ospf *po = ifa->proto;
  struct proto *p = (struct proto *) po;
  u8 i;
  int sendreq = 1;

  nrid = ntohl(ps->ospf_packet.routerid);

  if ((n = find_neigh(ifa, nrid)) == NULL)
  {
    OSPF_TRACE(D_PACKETS, "Received lsupd from unknown neighbor! (%I)", nrid);
    return;
  }

  if (n->state < NEIGHBOR_EXCHANGE)
  {
    OSPF_TRACE(D_PACKETS,
	       "Received lsupd in lesser state than EXCHANGE from (%I)",
	       n->ip);
    return;
  }

  if (size <=
      (sizeof(struct ospf_lsupd_packet) + sizeof(struct ospf_lsa_header)))
  {
    log(L_WARN "Received lsupd from %I is too short!", n->ip);
    return;
  }

  OSPF_TRACE(D_PACKETS, "Received LS upd from %I", n->ip);
  ospf_neigh_sm(n, INM_HELLOREC);	/* Questionable */

  lsa = (struct ospf_lsa_header *) (ps + 1);
  area = htonl(ps->ospf_packet.areaid);
  oa = ospf_find_area((struct proto_ospf *) p, area);

  for (i = 0; i < ntohl(ps->lsano); i++,
       lsa = (struct ospf_lsa_header *) (((u8 *) lsa) + ntohs(lsa->length)))
  {
    struct ospf_lsa_header lsatmp;
    struct top_hash_entry *lsadb;
    int diff = ((u8 *) lsa) - ((u8 *) ps);
    u16 chsum, lenn = ntohs(lsa->length);

    if (((diff + sizeof(struct ospf_lsa_header)) >= size)
	|| ((lenn + diff) > size))
    {
      log(L_WARN "Received lsupd from %I is too short!", n->ip);
      ospf_neigh_sm(n, INM_BADLSREQ);
      break;
    }

    if ((lenn <= sizeof(struct ospf_lsa_header))
	|| (lenn != (4 * (lenn / 4))))
    {
      log(L_WARN "Received LSA from %I with bad length", n->ip);
      ospf_neigh_sm(n, INM_BADLSREQ);
      break;
    }

    /* pg 143 (1) */
    chsum = lsa->checksum;
    if (chsum != lsasum_check(lsa, NULL, po))
    {
      log(L_WARN "Received bad lsa checksum from %I", n->ip);
      continue;
    }

    /* pg 143 (2) */
    if ((lsa->type < LSA_T_RT) || (lsa->type > LSA_T_EXT))
    {
      log(L_WARN "Unknown LSA type from %I", n->ip);
      continue;
    }

    /* pg 143 (3) */
    if ((lsa->type == LSA_T_EXT) && oa->stub)
    {
      log(L_WARN "Received External LSA in stub area from %I", n->ip);
      continue;
    }

    ntohlsah(lsa, &lsatmp);

    DBG("Update Type: %u ID: %I RT: %I, Sn: 0x%08x Age: %u, Sum: %u\n",
	lsatmp.type, lsatmp.id, lsatmp.rt, lsatmp.sn, lsatmp.age,
	lsatmp.checksum);

    lsadb = ospf_hash_find_header(oa->gr, &lsatmp);

#ifdef LOCAL_DEBUG
    if (lsadb)
      DBG("I have Type: %u ID: %I RT: %I, Sn: 0x%08x Age: %u, Sum: %u\n",
	  lsadb->lsa.type, lsadb->lsa.id, lsadb->lsa.rt, lsadb->lsa.sn,
	  lsadb->lsa.age, lsadb->lsa.checksum);
#endif

    /* pg 143 (4) */
    if ((lsatmp.age == LSA_MAXAGE) && (lsadb == NULL) && can_flush_lsa(oa))
    {
      ospf_lsack_enqueue(n, lsa, ACKL_DIRECT);
      continue;
    }

    /* pg 144 (5) */
    if ((lsadb == NULL) || (lsa_comp(&lsatmp, &lsadb->lsa) == CMP_NEWER))
    {
      struct ospf_iface *ift = NULL;
      void *body;
      struct ospf_iface *nifa;
      int self = (lsatmp.rt == p->cf->global->router_id);

      DBG("PG143(5): Received LSA is newer\n");

      /* pg 145 (5f) - premature aging of self originated lsa */
      if ((!self) && (lsatmp.type == LSA_T_NET))
      {
	WALK_LIST(nifa, po->iface_list)
	{
	  if (ipa_compare(nifa->iface->addr->ip, ipa_from_u32(lsatmp.id)) ==
	      0)
	  {
	    self = 1;
	    break;
	  }
	}
      }

      if (self)
      {
	struct top_hash_entry *en;

	if ((lsatmp.age == LSA_MAXAGE) && (lsatmp.sn == LSA_MAXSEQNO))
	{
	  ospf_lsack_enqueue(n, lsa, ACKL_DIRECT);
	  continue;
	}

	lsatmp.age = LSA_MAXAGE;
	lsatmp.sn = LSA_MAXSEQNO;
	lsa->age = htons(LSA_MAXAGE);
	lsa->sn = htonl(LSA_MAXSEQNO);
	OSPF_TRACE(D_EVENTS, "Premature aging self originated lsa.");
	OSPF_TRACE(D_EVENTS, "Type: %d, Id: %I, Rt: %I", lsatmp.type,
		   lsatmp.id, lsatmp.rt);
	lsasum_check(lsa, (lsa + 1), po);	/* It also calculates chsum! */
	lsatmp.checksum = ntohs(lsa->checksum);
	ospf_lsupd_flood(NULL, lsa, &lsatmp, NULL, oa, 0);
	if (en = ospf_hash_find_header(oa->gr, &lsatmp))
	{
	  ospf_lsupd_flood(NULL, NULL, &en->lsa, NULL, oa, 1);
	}
	continue;
      }

      /* pg 144 (5a) */
      if (lsadb && ((now - lsadb->inst_t) <= MINLSARRIVAL))	/* FIXME: test for flooding? */
      {
	DBG("I got it in less that MINLSARRIVAL\n");
	sendreq = 0;
	continue;
      }

      if (ospf_lsupd_flood(n, lsa, &lsatmp, ifa, ifa->oa, 1) == 0)
      {
	DBG("Wasn't flooded back\n");	/* ps 144(5e), pg 153 */
	if (ifa->state == OSPF_IS_BACKUP)
	{
	  if (ifa->drid == n->rid)
	    ospf_lsack_enqueue(n, lsa, ACKL_DELAY);
	}
	else
	  ospf_lsack_enqueue(n, lsa, ACKL_DELAY);
      }

      /* Remove old from all ret lists */
      /* pg 144 (5c) */
      if (lsadb)
	WALK_LIST(NODE ift, po->iface_list)
	  WALK_LIST(NODE ntmp, ift->neigh_list)
      {
	struct top_hash_entry *en;
	if (ntmp->state > NEIGHBOR_EXSTART)
	  if ((en = ospf_hash_find_header(ntmp->lsrth, &lsadb->lsa)) != NULL)
	  {
	    s_rem_node(SNODE en);
	    if (en->lsa_body != NULL)
	      mb_free(en->lsa_body);
	    en->lsa_body = NULL;
	    ospf_hash_delete(ntmp->lsrth, en);
	  }
      }

      if ((lsatmp.age == LSA_MAXAGE) && (lsatmp.sn == LSA_MAXSEQNO)
	  && lsadb && can_flush_lsa(oa))
      {
	flush_lsa(lsadb, oa);
	schedule_rtcalc(oa);
	continue;
      }				/* FIXME lsack? */

      /* pg 144 (5d) */
      body =
	mb_alloc(p->pool, lsatmp.length - sizeof(struct ospf_lsa_header));
      ntohlsab(lsa + 1, body, lsatmp.type,
	       lsatmp.length - sizeof(struct ospf_lsa_header));
      lsadb = lsa_install_new(&lsatmp, body, oa);
      DBG("New LSA installed in DB\n");

      continue;
    }

    /* FIXME pg145 (6) */

    /* pg145 (7) */
    if (lsa_comp(&lsatmp, &lsadb->lsa) == CMP_SAME)
    {
      struct top_hash_entry *en;
      DBG("PG145(7) Got the same LSA\n");
      if ((en = ospf_hash_find_header(n->lsrth, &lsadb->lsa)) != NULL)
      {
	/* pg145 (7a) */
	s_rem_node(SNODE en);
	if (en->lsa_body != NULL)
	  mb_free(en->lsa_body);
	en->lsa_body = NULL;
	ospf_hash_delete(n->lsrth, en);
	if (ifa->state == OSPF_IS_BACKUP)
	{
	  if (n->rid == ifa->drid)
	    ospf_lsack_enqueue(n, lsa, ACKL_DELAY);
	}
      }
      else
      {
	/* pg145 (7b) */
	ospf_lsack_enqueue(n, lsa, ACKL_DIRECT);
      }
      sendreq = 0;
      continue;
    }

    /* pg145 (8) */
    if ((lsadb->lsa.age == LSA_MAXAGE) && (lsadb->lsa.sn == LSA_MAXSEQNO))
    {
      continue;
    }

    {
      list l;
      struct l_lsr_head ll;
      init_list(&l);
      ll.lsh.id = lsadb->lsa.id;
      ll.lsh.rt = lsadb->lsa.rt;
      ll.lsh.type = lsadb->lsa.type;
      add_tail(&l, NODE & ll);
      ospf_lsupd_send_list(n, &l);
    }
  }

  /* Send direct LSAs */
  ospf_lsack_send(n, ACKL_DIRECT);

  if (sendreq && (n->state == NEIGHBOR_LOADING))
  {
    ospf_lsreq_send(n);		/* Ask for another part of neighbor's database */
  }
}

void
ospf_lsupd_flush_nlsa(struct top_hash_entry *en, struct ospf_area *oa)
{
  struct ospf_lsa_header *lsa = &en->lsa;
  struct proto_ospf *po = oa->po;
  struct proto *p = &po->proto;

  lsa->age = LSA_MAXAGE;
  lsa->sn = LSA_MAXSEQNO;
  lsasum_calculate(lsa, en->lsa_body, po);
  OSPF_TRACE(D_EVENTS, "Premature aging self originated lsa!");
  OSPF_TRACE(D_EVENTS, "Type: %d, Id: %I, Rt: %I", lsa->type,
	     lsa->id, lsa->rt);
  ospf_lsupd_flood(NULL, NULL, lsa, NULL, oa, 0);
}
