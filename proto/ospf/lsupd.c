/*
 *	BIRD -- OSPF
 *
 *	(c) 2000--2004 Ondrej Filip <feela@network.cz>
 *	(c) 2009--2014 Ondrej Zajicek <santiago@crfreenet.org>
 *	(c) 2009--2014 CZ.NIC z.s.p.o.
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"


/*
struct ospf_lsupd_packet
{
  struct ospf_packet hdr;
  // union ospf_auth auth;

  u32 lsa_count;
  void lsas[];
};
*/


void
ospf_dump_lsahdr(struct ospf_proto *p, struct ospf_lsa_header *lsa_n)
{
  struct ospf_lsa_header lsa;
  u32 lsa_etype;

  lsa_ntoh_hdr(lsa_n, &lsa);
  lsa_etype = lsa_get_etype(&lsa, p);

  log(L_TRACE "%s:     LSA      Type: %04x, Id: %R, Rt: %R, Age: %u, Seq: %08x, Sum: %04x",
      p->p.name, lsa_etype, lsa.id, lsa.rt, lsa.age, lsa.sn, lsa.checksum);
}

void
ospf_dump_common(struct ospf_proto *p, struct ospf_packet *pkt)
{
  log(L_TRACE "%s:     length   %d", p->p.name, ntohs(pkt->length));
  log(L_TRACE "%s:     router   %R", p->p.name, ntohl(pkt->routerid));
}

static inline uint
ospf_lsupd_hdrlen(struct ospf_proto *p)
{
  return ospf_pkt_hdrlen(p) + 4; /* + u32 lsa count field */
}

static inline u32
ospf_lsupd_get_lsa_count(struct ospf_packet *pkt, uint hdrlen)
{
  u32 *c = ((void *) pkt) + hdrlen - 4;
  return ntohl(*c);
}

static inline void
ospf_lsupd_set_lsa_count(struct ospf_packet *pkt, uint hdrlen, u32 val)
{
  u32 *c = ((void *) pkt) + hdrlen - 4;
  *c = htonl(val);
}

static inline void
ospf_lsupd_body(struct ospf_proto *p, struct ospf_packet *pkt,
		uint *offset, uint *bound, uint *lsa_count)
{
  uint hlen = ospf_lsupd_hdrlen(p);
  *offset = hlen;
  *bound = ntohs(pkt->length) - sizeof(struct ospf_lsa_header);
  *lsa_count = ospf_lsupd_get_lsa_count(pkt, hlen);
}

static void
ospf_dump_lsupd(struct ospf_proto *p, struct ospf_packet *pkt)
{
  uint offset, bound, i, lsa_count, lsalen;

  ASSERT(pkt->type == LSUPD_P);
  ospf_dump_common(p, pkt);

  ospf_lsupd_body(p, pkt, &offset, &bound, &lsa_count);
  for (i = 0; i < lsa_count; i++)
  {
    if (offset > bound)
    {
      log(L_TRACE "%s:     LSA      invalid", p->p.name);
      return;
    }

    struct ospf_lsa_header *lsa = ((void *) pkt) + offset;
    ospf_dump_lsahdr(p, lsa);
    lsalen = ntohs(lsa->length);
    offset += lsalen;

    if (((lsalen % 4) != 0) || (lsalen <= sizeof(struct ospf_lsa_header)))
    {
      log(L_TRACE "%s:     LSA      invalid", p->p.name);
      return;
    }
  }
}


static inline void
ospf_lsa_lsrt_up(struct top_hash_entry *en, struct ospf_neighbor *n)
{
  struct top_hash_entry *ret = ospf_hash_get_entry(n->lsrth, en);

  if (!SNODE_VALID(ret))
  {
    en->ret_count++;
    s_add_tail(&n->lsrtl, SNODE ret);
  }

  ret->lsa = en->lsa;
  ret->lsa_body = LSA_BODY_DUMMY;
}

static inline int
ospf_lsa_lsrt_down(struct top_hash_entry *en, struct ospf_neighbor *n)
{
  struct top_hash_entry *ret = ospf_hash_find_entry(n->lsrth, en);

  if (ret)
  {
    en->ret_count--;
    s_rem_node(SNODE ret);
    ospf_hash_delete(n->lsrth, ret);
    return 1;
  }

  return 0;
}

void
ospf_add_flushed_to_lsrt(struct ospf_proto *p, struct ospf_neighbor *n)
{
  struct top_hash_entry *en;

  WALK_SLIST(en, p->lsal)
    if ((en->lsa.age == LSA_MAXAGE) && (en->lsa_body != NULL) &&
	lsa_flooding_allowed(en->lsa_type, en->domain, n->ifa))
      ospf_lsa_lsrt_up(en, n);
}


static void ospf_send_lsupd_to_ifa(struct ospf_proto *p, struct top_hash_entry *en, struct ospf_iface *ifa);


/**
 * ospf_flood_lsa - send LSA to the neighbors
 * @p: OSPF protocol instance
 * @en: LSA entry
 * @from: neighbor than sent this LSA (or NULL if LSA is local)
 *
 * return value - was the LSA flooded back?
 */
int
ospf_flood_lsa(struct ospf_proto *p, struct top_hash_entry *en, struct ospf_neighbor *from)
{
  struct ospf_iface *ifa;
  struct ospf_neighbor *n;

  /* RFC 2328 13.3 */

  int back = 0;
  WALK_LIST(ifa, p->iface_list)
  {
    if (ifa->stub)
      continue;

    if (! lsa_flooding_allowed(en->lsa_type, en->domain, ifa))
      continue;

    DBG("Wanted to flood LSA: Type: %u, ID: %R, RT: %R, SN: 0x%x, Age %u\n",
	hh->type, hh->id, hh->rt, hh->sn, hh->age);

    int used = 0;
    WALK_LIST(n, ifa->neigh_list)
    {
      /* 13.3 (1a) */
      if (n->state < NEIGHBOR_EXCHANGE)
	continue;

      /* 13.3 (1b) */
      if (n->state < NEIGHBOR_FULL)
      {
	struct top_hash_entry *req = ospf_hash_find_entry(n->lsrqh, en);
	if (req != NULL)
	{
	  int cmp = lsa_comp(&en->lsa, &req->lsa);

	  /* If same or newer, remove LSA from the link state request list */
	  if (cmp > CMP_OLDER)
	  {
	    s_rem_node(SNODE req);
	    ospf_hash_delete(n->lsrqh, req);
	    n->want_lsreq = 1;

	    if ((EMPTY_SLIST(n->lsrql)) && (n->state == NEIGHBOR_LOADING))
	      ospf_neigh_sm(n, INM_LOADDONE);
	  }

	  /* If older or same, skip processing of this neighbor */
	  if (cmp < CMP_NEWER)
	    continue;
	}
      }

      /* 13.3 (1c) */
      if (n == from)
	continue;

      /* In OSPFv3, there should be check whether receiving router understand
	 that type of LSA (for LSA types with U-bit == 0). But as we do not support
	 any optional LSA types, this is not needed yet */

      /* 13.3 (1d) - add LSA to the link state retransmission list */
      ospf_lsa_lsrt_up(en, n);

      used = 1;
    }

    /* 13.3 (2) */
    if (!used)
      continue;

    if (from && (from->ifa == ifa))
    {
      /* 13.3 (3) */
      if ((from->rid == ifa->drid) || (from->rid == ifa->bdrid))
	continue;

      /* 13.3 (4) */
      if (ifa->state == OSPF_IS_BACKUP)
	continue;

      back = 1;
    }

    /* 13.3 (5) - finally flood the packet */
    ospf_send_lsupd_to_ifa(p, en, ifa);
  }

  return back;
}

static uint
ospf_prepare_lsupd(struct ospf_proto *p, struct ospf_iface *ifa,
		   struct top_hash_entry **lsa_list, uint lsa_count)
{
  struct ospf_packet *pkt;
  uint hlen, pos, i, maxsize;

  pkt = ospf_tx_buffer(ifa);
  hlen = ospf_lsupd_hdrlen(p);
  maxsize = ospf_pkt_maxsize(ifa);

  ospf_pkt_fill_hdr(ifa, pkt, LSUPD_P);
  pos = hlen;

  for (i = 0; i < lsa_count; i++)
  {
    struct top_hash_entry *en = lsa_list[i];
    uint len = en->lsa.length;

    if ((pos + len) > maxsize)
    {
      /* The packet if full, stop adding LSAs and sent it */
      if (i > 0)
	break;

      /* LSA is larger than MTU, check buffer size */
      if (ospf_iface_assure_bufsize(ifa, pos + len) < 0)
      {
	/* Cannot fit in a tx buffer, skip that */
	log(L_ERR "%s: LSA too large to send on %s (Type: %04x, Id: %R, Rt: %R)",
	    p->p.name, ifa->ifname, en->lsa_type, en->lsa.id, en->lsa.rt);
	break;
      }

      /* TX buffer could be reallocated */
      pkt = ospf_tx_buffer(ifa);
    }

    struct ospf_lsa_header *buf = ((void *) pkt) + pos;
    lsa_hton_hdr(&en->lsa, buf);
    lsa_hton_body(en->lsa_body, ((void *) buf) + sizeof(struct ospf_lsa_header),
		  len - sizeof(struct ospf_lsa_header));
    buf->age = htons(MIN(en->lsa.age + ifa->inftransdelay, LSA_MAXAGE));

    pos += len;
  }

  ospf_lsupd_set_lsa_count(pkt, hlen, i);
  pkt->length = htons(pos);

  return i;
}


static void
ospf_send_lsupd_to_ifa(struct ospf_proto *p, struct top_hash_entry *en, struct ospf_iface *ifa)
{
  uint c = ospf_prepare_lsupd(p, ifa, &en, 1);

  if (!c)	/* Too large LSA */
    return;

  OSPF_PACKET(ospf_dump_lsupd, ospf_tx_buffer(ifa),
	      "LSUPD packet flooded via %s", ifa->ifname);

  if (ifa->type == OSPF_IT_BCAST)
  {
    if ((ifa->state == OSPF_IS_DR) || (ifa->state == OSPF_IS_BACKUP))
      ospf_send_to_all(ifa);
    else
      ospf_send_to_des(ifa);
  }
  else
    ospf_send_to_agt(ifa, NEIGHBOR_EXCHANGE);
}

int
ospf_send_lsupd(struct ospf_proto *p, struct top_hash_entry **lsa_list, uint lsa_count, struct ospf_neighbor *n)
{
  struct ospf_iface *ifa = n->ifa;
  uint i, c;

  for (i = 0; i < lsa_count; i += c)
  {
    c = ospf_prepare_lsupd(p, ifa, lsa_list + i, lsa_count - i);

    if (!c)	/* Too large LSA */
      { i++; continue; }

    OSPF_PACKET(ospf_dump_lsupd, ospf_tx_buffer(ifa),
		"LSUPD packet sent to %I via %s", n->ip, ifa->ifname);

    ospf_send_to(ifa, n->ip);
  }

  return lsa_count;
}

void
ospf_rxmt_lsupd(struct ospf_proto *p, struct ospf_neighbor *n)
{
  const uint max = 128;
  struct top_hash_entry *entries[max];
  struct top_hash_entry *ret, *nxt, *en;
  uint i = 0;

  WALK_SLIST_DELSAFE(ret, nxt, n->lsrtl)
  {
    if (i == max)
      break;

    en = ospf_hash_find_entry(p->gr, ret);
    if (!en)
    {
      /* Probably flushed LSA, this should not happen */
      log(L_WARN "%s: LSA disappeared (Type: %04x, Id: %R, Rt: %R)",
	  p->p.name, ret->lsa_type, ret->lsa.id, ret->lsa.rt);

      s_rem_node(SNODE ret);
      ospf_hash_delete(n->lsrth, ret);

      continue;
    }

    entries[i] = en;
    i++;
  }

  ospf_send_lsupd(p, entries, i, n);
}


static inline int
ospf_addr_is_local(struct ospf_proto *p, struct ospf_area *oa, ip_addr ip)
{
  struct ospf_iface *ifa;
  WALK_LIST(ifa, p->iface_list)
    if ((ifa->oa == oa) && ifa->addr && ipa_equal(ifa->addr->ip, ip))
      return 1;

  return 0;
}

void
ospf_receive_lsupd(struct ospf_packet *pkt, struct ospf_iface *ifa,
		   struct ospf_neighbor *n)
{
  struct ospf_proto *p = ifa->oa->po;

  /* RFC 2328 13. */

  int skip_lsreq = 0;
  n->want_lsreq = 0;

  uint plen = ntohs(pkt->length);
  if (plen < (ospf_lsupd_hdrlen(p) + sizeof(struct ospf_lsa_header)))
  {
    log(L_ERR "OSPF: Bad LSUPD packet from %I - too short (%u B)", n->ip, plen);
    return;
  }

  OSPF_PACKET(ospf_dump_lsupd, pkt, "LSUPD packet received from %I via %s", n->ip, ifa->ifname);

  if (n->state < NEIGHBOR_EXCHANGE)
  {
    OSPF_TRACE(D_PACKETS, "Received lsupd in lesser state than EXCHANGE from (%I)", n->ip);
    return;
  }

  ospf_neigh_sm(n, INM_HELLOREC);	/* Questionable */

  uint offset, bound, i, lsa_count;
  ospf_lsupd_body(p, pkt, &offset, &bound, &lsa_count);

  for (i = 0; i < lsa_count; i++)
  {
    struct ospf_lsa_header lsa, *lsa_n;
    struct top_hash_entry *en;
    u32 lsa_len, lsa_type, lsa_domain;

    if (offset > bound)
    {
      log(L_WARN "%s: Received LSUPD from %I is too short", p->p.name, n->ip);
      ospf_neigh_sm(n, INM_BADLSREQ);
      return;
    }

    /* LSA header in network order */
    lsa_n = ((void *) pkt) + offset;
    lsa_len = ntohs(lsa_n->length);
    offset += lsa_len;

    if ((offset > plen) || ((lsa_len % 4) != 0) ||
	(lsa_len <= sizeof(struct ospf_lsa_header)))
    {
      log(L_WARN "%s: Received LSA from %I with bad length", p->p.name, n->ip);
      ospf_neigh_sm(n, INM_BADLSREQ);
      return;
    }

    /* RFC 2328 13. (1) - validate LSA checksum */
    u16 chsum = lsa_n->checksum;
    if (chsum != lsasum_check(lsa_n, NULL))
    {
      log(L_WARN "%s: Received LSA from %I with bad checksum: %x %x",
	  p->p.name, n->ip, chsum, lsa_n->checksum);
      continue;
    }

    /* LSA header in host order */
    lsa_ntoh_hdr(lsa_n, &lsa);
    lsa_get_type_domain(&lsa, ifa, &lsa_type, &lsa_domain);

    DBG("Update Type: %04x, Id: %R, Rt: %R, Sn: 0x%08x, Age: %u, Sum: %u\n",
	lsa_type, lsa.id, lsa.rt, lsa.sn, lsa.age, lsa.checksum);

    /* RFC 2328 13. (2) */
    if (!lsa_type)
    {
      log(L_WARN "%s: Received unknown LSA type from %I", p->p.name, n->ip);
      continue;
    }

    /* RFC 5340 4.5.1 (2) and RFC 2328 13. (3) */
    if (!oa_is_ext(ifa->oa) && (LSA_SCOPE(lsa_type) == LSA_SCOPE_AS))
    {
      log(L_WARN "%s: Received LSA with AS scope in stub area from %I", p->p.name, n->ip);
      continue;
    }

    /* Errata 3746 to RFC 2328 - rt-summary-LSAs forbidden in stub areas */
    if (!oa_is_ext(ifa->oa) && (lsa_type == LSA_T_SUM_RT))
    {
      log(L_WARN "%s: Received rt-summary-LSA in stub area from %I", p->p.name, n->ip);
      continue;
    }

    /* RFC 5340 4.5.1 (3) */
    if (LSA_SCOPE(lsa_type) == LSA_SCOPE_RES)
    {
      log(L_WARN "%s: Received LSA with invalid scope from %I", p->p.name, n->ip);
      continue;
    }

    /* Find local copy of LSA in link state database */
    en = ospf_hash_find(p->gr, lsa_domain, lsa.id, lsa.rt, lsa_type);

#ifdef LOCAL_DEBUG
    if (en)
      DBG("I have Type: %04x, Id: %R, Rt: %R, Sn: 0x%08x, Age: %u, Sum: %u\n",
	  en->lsa_type, en->lsa.id, en->lsa.rt, en->lsa.sn, en->lsa.age, en->lsa.checksum);
#endif

    /* 13. (4) - ignore maxage LSA if i have no local copy */
    if ((lsa.age == LSA_MAXAGE) && !en && (p->padj == 0))
    {
      /* 13.5. - schedule ACKs (tbl 19, case 5) */
      ospf_enqueue_lsack(n, lsa_n, ACKL_DIRECT);
      continue;
    }

    /* 13. (5) - received LSA is newer (or no local copy) */
    if (!en || (lsa_comp(&lsa, &en->lsa) == CMP_NEWER))
    {
      /* 13. (5a) - enforce minimum time between updates for received LSAs */
      /* We also use this to ratelimit reactions to received self-originated LSAs */
      if (en && ((now - en->inst_time) < MINLSARRIVAL))
      {
	OSPF_TRACE(D_EVENTS, "Skipping LSA received in less that MinLSArrival");
	skip_lsreq = 1;
	continue;
      }

      /* Copy and validate LSA body */
      int blen = lsa.length - sizeof(struct ospf_lsa_header);
      void *body = mb_alloc(p->p.pool, blen);
      lsa_ntoh_body(lsa_n + 1, body, blen);

      if (lsa_validate(&lsa, lsa_type, ospf_is_v2(p), body) == 0)
      {
	log(L_WARN "%s: Received invalid LSA from %I", p->p.name, n->ip);
	mb_free(body);
	continue;
      }

      /* 13. (5f) - handle self-originated LSAs, see also 13.4. */
      if ((lsa.rt == p->router_id) ||
	  (ospf_is_v2(p) && (lsa_type == LSA_T_NET) && ospf_addr_is_local(p, ifa->oa, ipa_from_u32(lsa.id))))
      {
	OSPF_TRACE(D_EVENTS, "Received unexpected self-originated LSA");
	ospf_advance_lsa(p, en, &lsa, lsa_type, lsa_domain, body);
	continue;
      }

      /* 13. (5c) - remove old LSA from all retransmission lists */
      /*
       * We only need to remove it from the retransmission list of the neighbor
       * that send us the new LSA. The old LSA is automatically replaced in
       * retransmission lists by the new LSA.
       */
      if (en)
	ospf_lsa_lsrt_down(en, n);

#if 0
      /*
       * Old code for removing LSA from all retransmission lists. Must be done
       * before (5b), otherwise it also removes the new entries from (5b).
       */
      struct ospf_iface *ifi;
      struct ospf_neighbor *ni;

      WALK_LIST(ifi, p->iface_list)
	WALK_LIST(ni, ifi->neigh_list)
	if (ni->state > NEIGHBOR_EXSTART)
	  ospf_lsa_lsrt_down(en, ni);
#endif

      /* 13. (5d) - install new LSA into database */
      en = ospf_install_lsa(p, &lsa, lsa_type, lsa_domain, body);

      /* RFC 5340 4.4.3 Events 6+7 - new Link LSA received */
      if (lsa_type == LSA_T_LINK)
	ospf_notify_net_lsa(ifa);

      /* 13. (5b) - flood new LSA */
      int flood_back = ospf_flood_lsa(p, en, n);

      /* 13.5. - schedule ACKs (tbl 19, cases 1+2) */
      if (! flood_back)
	if ((ifa->state != OSPF_IS_BACKUP) || (n->rid == ifa->drid))
	  ospf_enqueue_lsack(n, lsa_n, ACKL_DELAY);

      /* FIXME: remove LSA entry if it is LSA_MAXAGE and it is possible? */

      continue;
    }

    /* FIXME pg145 (6) */

    /* 13. (7) - received LSA is same */
    if (lsa_comp(&lsa, &en->lsa) == CMP_SAME)
    {
      /* Duplicate LSA, treat as implicit ACK */
      int implicit_ack = ospf_lsa_lsrt_down(en, n);

      /* 13.5. - schedule ACKs (tbl 19, cases 3+4) */
      if (implicit_ack)
      {
	if ((ifa->state == OSPF_IS_BACKUP) && (n->rid == ifa->drid))
	  ospf_enqueue_lsack(n, lsa_n, ACKL_DELAY);
      }
      else
	ospf_enqueue_lsack(n, lsa_n, ACKL_DIRECT);

      skip_lsreq = 1;
      continue;
    }

    /* 13. (8) - received LSA is older */
    {
      /* Seqnum is wrapping, wait until it is flushed */
      if ((en->lsa.age == LSA_MAXAGE) && (en->lsa.sn == LSA_MAXSEQNO))
	continue;

      /* Send newer local copy back to neighbor */
      /* FIXME - check for MinLSArrival ? */
      ospf_send_lsupd(p, &en, 1, n);
    }
  }

  /* Send direct LSACKs */
  ospf_send_lsack(p, n, ACKL_DIRECT);

  /*
   * In loading state, we should ask for another batch of LSAs. This is only
   * vaguely mentioned in RFC 2328. We send a new LSREQ only if the current
   * LSUPD actually removed some entries from LSA request list (want_lsreq) and
   * did not contain duplicate or early LSAs (skip_lsreq). The first condition
   * prevents endless floods, the second condition helps with flow control.
   */
  if ((n->state == NEIGHBOR_LOADING) && n->want_lsreq && !skip_lsreq)
    ospf_send_lsreq(p, n);
}
