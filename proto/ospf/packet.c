/*
 *	BIRD -- OSPF
 *
 *	(c) 1999--2005 Ondrej Filip <feela@network.cz>
 *	(c) 2009--2014 Ondrej Zajicek <santiago@crfreenet.org>
 *	(c) 2009--2014 CZ.NIC z.s.p.o.
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"
#include "nest/password.h"
#include "lib/md5.h"

void
ospf_pkt_fill_hdr(struct ospf_iface *ifa, void *buf, u8 h_type)
{
  struct ospf_proto *p = ifa->oa->po;
  struct ospf_packet *pkt;

  pkt = (struct ospf_packet *) buf;

  pkt->version = ospf_get_version(p);
  pkt->type = h_type;
  pkt->length = htons(ospf_pkt_maxsize(ifa));
  pkt->routerid = htonl(p->router_id);
  pkt->areaid = htonl(ifa->oa->areaid);
  pkt->checksum = 0;
  pkt->instance_id = ifa->instance_id;
  pkt->autype = ifa->autype;
}

uint
ospf_pkt_maxsize(struct ospf_iface *ifa)
{
  uint headers = SIZE_OF_IP_HEADER;

  /* Relevant just for OSPFv2 */
  if (ifa->autype == OSPF_AUTH_CRYPT)
    headers += OSPF_AUTH_CRYPT_SIZE;

  return ifa->tx_length - headers;
}

/* We assume OSPFv2 in ospf_pkt_finalize() */
static void
ospf_pkt_finalize(struct ospf_iface *ifa, struct ospf_packet *pkt)
{
  struct password_item *passwd = NULL;
  union ospf_auth *auth = (void *) (pkt + 1);
  uint plen = ntohs(pkt->length);

  pkt->checksum = 0;
  pkt->autype = ifa->autype;
  bzero(auth, sizeof(union ospf_auth));

  /* Compatibility note: auth may contain anything if autype is
     none, but nonzero values do not work with Mikrotik OSPF */

  switch (ifa->autype)
  {
  case OSPF_AUTH_SIMPLE:
    passwd = password_find(ifa->passwords, 1);
    if (!passwd)
    {
      log(L_ERR "No suitable password found for authentication");
      return;
    }
    password_cpy(auth->password, passwd->password, sizeof(union ospf_auth));

  case OSPF_AUTH_NONE:
    {
      void *body = (void *) (auth + 1);
      uint blen = plen - sizeof(struct ospf_packet) - sizeof(union ospf_auth);
      pkt->checksum = ipsum_calculate(pkt, sizeof(struct ospf_packet), body, blen, NULL);
    }
    break;

  case OSPF_AUTH_CRYPT:
    passwd = password_find(ifa->passwords, 0);
    if (!passwd)
    {
      log(L_ERR "No suitable password found for authentication");
      return;
    }

    /* Perhaps use random value to prevent replay attacks after
       reboot when system does not have independent RTC? */
    if (!ifa->csn)
    {
      ifa->csn = (u32) now;
      ifa->csn_use = now;
    }

    /* We must have sufficient delay between sending a packet and increasing 
       CSN to prevent reordering of packets (in a network) with different CSNs */
    if ((now - ifa->csn_use) > 1)
      ifa->csn++;

    ifa->csn_use = now;

    auth->md5.zero = 0;
    auth->md5.keyid = passwd->id;
    auth->md5.len = OSPF_AUTH_CRYPT_SIZE;
    auth->md5.csn = htonl(ifa->csn);

    void *tail = ((void *) pkt) + plen;
    char password[OSPF_AUTH_CRYPT_SIZE];
    password_cpy(password, passwd->password, OSPF_AUTH_CRYPT_SIZE);

    struct MD5Context ctxt;
    MD5Init(&ctxt);
    MD5Update(&ctxt, (char *) pkt, plen);
    MD5Update(&ctxt, password, OSPF_AUTH_CRYPT_SIZE);
    MD5Final(tail, &ctxt);
    break;

  default:
    bug("Unknown authentication type");
  }
}

/* We assume OSPFv2 in ospf_pkt_checkauth() */
static int
ospf_pkt_checkauth(struct ospf_neighbor *n, struct ospf_iface *ifa, struct ospf_packet *pkt, int size)
{
  struct ospf_proto *p = ifa->oa->po;
  union ospf_auth *auth = (void *) (pkt + 1);
  struct password_item *pass = NULL, *ptmp;
  char password[OSPF_AUTH_CRYPT_SIZE];

  uint plen = ntohs(pkt->length);
  u8 autype = pkt->autype;

  if (autype != ifa->autype)
  {
    OSPF_TRACE(D_PACKETS, "OSPF_auth: Method differs (%d)", autype);
    return 0;
  }

  switch (autype)
  {
  case OSPF_AUTH_NONE:
    return 1;

  case OSPF_AUTH_SIMPLE:
    pass = password_find(ifa->passwords, 1);
    if (!pass)
    {
      OSPF_TRACE(D_PACKETS, "OSPF_auth: no password found");
      return 0;
    }

    password_cpy(password, pass->password, sizeof(union ospf_auth));
    if (memcmp(auth->password, password, sizeof(union ospf_auth)))
    {
      OSPF_TRACE(D_PACKETS, "OSPF_auth: different passwords");
      return 0;
    }
    return 1;

  case OSPF_AUTH_CRYPT:
    if (auth->md5.len != OSPF_AUTH_CRYPT_SIZE)
    {
      OSPF_TRACE(D_PACKETS, "OSPF_auth: wrong size of md5 digest");
      return 0;
    }

    if (plen + OSPF_AUTH_CRYPT_SIZE > size)
    {
      OSPF_TRACE(D_PACKETS, "OSPF_auth: size mismatch (%d vs %d)",
		 plen + OSPF_AUTH_CRYPT_SIZE, size);
      return 0;
    }

    if (n)
    {
      u32 rcv_csn = ntohl(auth->md5.csn);
      if(rcv_csn < n->csn)
      {
	OSPF_TRACE(D_PACKETS, "OSPF_auth: lower sequence number (rcv %d, old %d)", rcv_csn, n->csn);
	return 0;
      }

      n->csn = rcv_csn;
    }

    if (ifa->passwords)
    {
      WALK_LIST(ptmp, *(ifa->passwords))
      {
	if (auth->md5.keyid != ptmp->id) continue;
	if ((ptmp->accfrom > now_real) || (ptmp->accto < now_real)) continue;
	pass = ptmp;
	break;
      }
    }

    if (!pass)
    {
      OSPF_TRACE(D_PACKETS, "OSPF_auth: no suitable md5 password found");
      return 0;
    }

    void *tail = ((void *) pkt) + plen;
    char md5sum[OSPF_AUTH_CRYPT_SIZE];
    password_cpy(password, pass->password, OSPF_AUTH_CRYPT_SIZE);

    struct MD5Context ctxt;
    MD5Init(&ctxt);
    MD5Update(&ctxt, (char *) pkt, plen);
    MD5Update(&ctxt, password, OSPF_AUTH_CRYPT_SIZE);
    MD5Final(md5sum, &ctxt);

    if (memcmp(md5sum, tail, OSPF_AUTH_CRYPT_SIZE))
    {
      OSPF_TRACE(D_PACKETS, "OSPF_auth: wrong md5 digest");
      return 0;
    }
    return 1;

  default:
    OSPF_TRACE(D_PACKETS, "OSPF_auth: unknown auth type");
    return 0;
  }
}


/**
 * ospf_rx_hook
 * @sk: socket we received the packet.
 * @size: size of the packet
 *
 * This is the entry point for messages from neighbors. Many checks (like
 * authentication, checksums, size) are done before the packet is passed to
 * non generic functions.
 */
int
ospf_rx_hook(sock *sk, int size)
{
  char *mesg = "OSPF: Bad packet from ";

  /* We want just packets from sk->iface. Unfortunately, on BSD we
     cannot filter out other packets at kernel level and we receive
     all packets on all sockets */
  if (sk->lifindex != sk->iface->index)
    return 1;

  DBG("OSPF: RX hook called (iface %s, src %I, dst %I)\n",
      sk->ifname, sk->faddr, sk->laddr);

  /* Initially, the packet is associated with the 'master' iface */
  struct ospf_iface *ifa = sk->data;
  struct ospf_proto *p = ifa->oa->po;

  int src_local, dst_local, dst_mcast; 
  src_local = ipa_in_net(sk->faddr, ifa->addr->prefix, ifa->addr->pxlen);
  dst_local = ipa_equal(sk->laddr, ifa->addr->ip);
  dst_mcast = ipa_equal(sk->laddr, ifa->all_routers) || ipa_equal(sk->laddr, ifa->des_routers);

  if (ospf_is_v2(p))
  {
    /* First, we eliminate packets with strange address combinations.
     * In OSPFv2, they might be for other ospf_ifaces (with different IP
     * prefix) on the same real iface, so we don't log it. We enforce
     * that (src_local || dst_local), therefore we are eliminating all
     * such cases. 
     */
    if (dst_mcast && !src_local)
      return 1;
    if (!dst_mcast && !dst_local)
      return 1;

    /* Ignore my own broadcast packets */
    if (ifa->cf->real_bcast && ipa_equal(sk->faddr, ifa->addr->ip))
      return 1;
  }
  else
  {
    /* In OSPFv3, src_local and dst_local mean link-local. 
     * RFC 5340 says that local (non-vlink) packets use
     * link-local src address, but does not enforce it. Strange.
     */
    if (dst_mcast && !src_local)
      log(L_WARN "OSPF: Received multicast packet from %I (not link-local)", sk->faddr);
  }

  /* Second, we check packet size, checksum, and the protocol version */
  struct ospf_packet *pkt = (struct ospf_packet *) ip_skip_header(sk->rbuf, &size);

  if (pkt == NULL)
  {
    log(L_ERR "%s%I - bad IP header", mesg, sk->faddr);
    return 1;
  }

  if (ifa->check_ttl && (sk->rcv_ttl < 255))
  {
    log(L_ERR "%s%I - TTL %d (< 255)", mesg, sk->faddr, sk->rcv_ttl);
    return 1;
  }

  if ((uint) size < sizeof(struct ospf_packet))
  {
    log(L_ERR "%s%I - too short (%u bytes)", mesg, sk->faddr, size);
    return 1;
  }

  uint plen = ntohs(pkt->length);
  if ((plen < sizeof(struct ospf_packet)) || ((plen % 4) != 0))
  {
    log(L_ERR "%s%I - invalid length (%u)", mesg, sk->faddr, plen);
    return 1;
  }

  if (sk->flags & SKF_TRUNCATED)
  {
    log(L_WARN "%s%I - too large (%d/%d)", mesg, sk->faddr, plen, size);

    /* If we have dynamic buffers and received truncated message, we expand RX buffer */

    uint bs = plen + 256;
    bs = BIRD_ALIGN(bs, 1024);

    if (!ifa->cf->rx_buffer && (bs > sk->rbsize))
      sk_set_rbsize(sk, bs);

    return 1;
  }

  if (plen > size)
  {
    log(L_ERR "%s%I - size field does not match (%d/%d)", mesg, sk->faddr, plen, size);
    return 1;
  }

  if (pkt->version != ospf_get_version(p))
  {
    log(L_ERR "%s%I - version %u", mesg, sk->faddr, pkt->version);
    return 1;
  }

  if (ospf_is_v2(p) && (pkt->autype != OSPF_AUTH_CRYPT))
  {
    uint hlen = sizeof(struct ospf_packet) + sizeof(union ospf_auth);
    uint blen = plen - hlen;
    void *body = ((void *) pkt) + hlen;

    if (! ipsum_verify(pkt, sizeof(struct ospf_packet), body, blen, NULL))
    {
      log(L_ERR "%s%I - bad checksum", mesg, sk->faddr);
      return 1;
    }
  }

  /* Third, we resolve associated iface and handle vlinks. */

  u32 areaid = ntohl(pkt->areaid);
  u32 rid = ntohl(pkt->routerid);
  u8 instance_id = pkt->instance_id;

  if (areaid == ifa->oa->areaid)
  {
    /* Matching area ID */

    if (instance_id != ifa->instance_id)
      return 1;

    /* It is real iface, source should be local (in OSPFv2) */
    if (ospf_is_v2(p) && !src_local)
    {
      log(L_ERR "%s%I - strange source address for %s", mesg, sk->faddr, ifa->ifname);
      return 1;
    }

    goto found;
  }
  else if ((areaid == 0) && !dst_mcast)
  {
    /* Backbone area ID and possible vlink packet */

    if ((p->areano == 1) || !oa_is_ext(ifa->oa))
      return 1;

    struct ospf_iface *iff = NULL;
    WALK_LIST(iff, p->iface_list)
    {
      if ((iff->type == OSPF_IT_VLINK) && 
	  (iff->voa == ifa->oa) &&
	  (iff->instance_id == instance_id) &&
	  (iff->vid == rid))
      {
	/* Vlink should be UP */
	if (iff->state != OSPF_IS_PTP)
	  return 1;

	ifa = iff;
	goto found;
      }
    }

    /*
     * Cannot find matching vlink. It is either misconfigured vlink; NBMA or
     * PtMP with misconfigured area ID, or packet for some other instance (that
     * is possible even if instance_id == ifa->instance_id, because it may be
     * also vlink packet in the other instance, which is different namespace).
     */

    return 1;
  }
  else
  {
    /* Non-matching area ID but cannot be vlink packet */

    if (instance_id != ifa->instance_id)
      return 1;

    log(L_ERR "%s%I - area does not match (%R vs %R)",
	mesg, sk->faddr, areaid, ifa->oa->areaid);
    return 1;
  }


 found:
  if (ifa->stub)	    /* This shouldn't happen */
    return 1;

  if (ipa_equal(sk->laddr, ifa->des_routers) && (ifa->sk_dr == 0))
    return 1;

  if (rid == p->router_id)
  {
    log(L_ERR "%s%I - received my own router ID!", mesg, sk->faddr);
    return 1;
  }

  if (rid == 0)
  {
    log(L_ERR "%s%I - router id = 0.0.0.0", mesg, sk->faddr);
    return 1;
  }

  /* In OSPFv2, neighbors are identified by either IP or Router ID, base on network type */
  uint t = ifa->type;
  struct ospf_neighbor *n;
  if (ospf_is_v2(p) && ((t == OSPF_IT_BCAST) || (t == OSPF_IT_NBMA) || (t == OSPF_IT_PTMP)))
    n = find_neigh_by_ip(ifa, sk->faddr);
  else
    n = find_neigh(ifa, rid);

  if (!n && (pkt->type != HELLO_P))
  {
    log(L_WARN "OSPF: Received non-hello packet from unknown neighbor (src %I, iface %s)",
	sk->faddr, ifa->ifname);
    return 1;
  }

  if (ospf_is_v2(p) && !ospf_pkt_checkauth(n, ifa, pkt, size))
  {
    log(L_ERR "%s%I - authentication failed", mesg, sk->faddr);
    return 1;
  }

  switch (pkt->type)
  {
  case HELLO_P:
    ospf_receive_hello(pkt, ifa, n, sk->faddr);
    break;

  case DBDES_P:
    ospf_receive_dbdes(pkt, ifa, n);
    break;

  case LSREQ_P:
    ospf_receive_lsreq(pkt, ifa, n);
    break;

  case LSUPD_P:
    ospf_receive_lsupd(pkt, ifa, n);
    break;

  case LSACK_P:
    ospf_receive_lsack(pkt, ifa, n);
    break;

  default:
    log(L_ERR "%s%I - wrong type %u", mesg, sk->faddr, pkt->type);
    return 1;
  };
  return 1;
}

/*
void
ospf_tx_hook(sock * sk)
{
  struct ospf_iface *ifa= (struct ospf_iface *) (sk->data);
//  struct proto *p = (struct proto *) (ifa->oa->p);
  log(L_ERR "OSPF: TX hook called on %s", ifa->ifname);
}
*/

void
ospf_err_hook(sock * sk, int err)
{
  struct ospf_iface *ifa= (struct ospf_iface *) (sk->data);
  struct ospf_proto *p = ifa->oa->po;
  log(L_ERR "%s: Socket error on %s: %M", p->p.name, ifa->ifname, err);
}

void
ospf_verr_hook(sock *sk, int err)
{
  struct ospf_proto *p = (struct ospf_proto *) (sk->data);
  log(L_ERR "%s: Vlink socket error: %M", p->p.name, err);
}

void
ospf_send_to(struct ospf_iface *ifa, ip_addr dst)
{
  sock *sk = ifa->sk;
  struct ospf_packet *pkt = (struct ospf_packet *) sk->tbuf;
  int plen = ntohs(pkt->length);

  if (ospf_is_v2(ifa->oa->po))
  {
    if (ifa->autype == OSPF_AUTH_CRYPT)
      plen += OSPF_AUTH_CRYPT_SIZE;

    ospf_pkt_finalize(ifa, pkt);
  }

  int done = sk_send_to(sk, plen, dst, 0);
  if (!done)
    log(L_WARN "OSPF: TX queue full on %s", ifa->ifname);
}

void
ospf_send_to_agt(struct ospf_iface *ifa, u8 state)
{
  struct ospf_neighbor *n;

  WALK_LIST(n, ifa->neigh_list)
    if (n->state >= state)
      ospf_send_to(ifa, n->ip);
}

void
ospf_send_to_bdr(struct ospf_iface *ifa)
{
  if (ipa_nonzero(ifa->drip))
    ospf_send_to(ifa, ifa->drip);
  if (ipa_nonzero(ifa->bdrip))
    ospf_send_to(ifa, ifa->bdrip);
}
