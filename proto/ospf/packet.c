/*
 *	BIRD -- OSPF
 *
 *	(c) 1999 - 2003 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

void
fill_ospf_pkt_hdr(struct ospf_iface *ifa, void *buf, u8 h_type)
{
  struct ospf_packet *pkt;
  struct proto *p;

  p = (struct proto *) (ifa->proto);

  pkt = (struct ospf_packet *) buf;

  pkt->version = OSPF_VERSION;

  pkt->type = h_type;

  pkt->routerid = htonl(p->cf->global->router_id);
  pkt->areaid = htonl(ifa->an);
  pkt->autype = htons(ifa->autype);
  pkt->checksum = 0;
}

void
ospf_tx_authenticate(struct ospf_iface *ifa, struct ospf_packet *pkt)
{
  pkt->autype = htons(ifa->autype);
  memcpy(pkt->authetication, ifa->aukey, 8);
  return;
}

static int
ospf_rx_authenticate(struct ospf_iface *ifa, struct ospf_packet *pkt)
{
  int i;
  if (pkt->autype != htons(ifa->autype))
    return 0;
  if (ifa->autype == AU_NONE)
    return 1;
  if (ifa->autype == AU_SIMPLE)
  {
    for (i = 0; i < 8; i++)
    {
      if (pkt->authetication[i] != ifa->aukey[i])
	return 0;
    }
    return 1;
  }
  return 0;
}

void
ospf_pkt_finalize(struct ospf_iface *ifa, struct ospf_packet *pkt)
{

  ospf_tx_authenticate(ifa, pkt);

  /* Count checksum */
  pkt->checksum = ipsum_calculate(pkt, sizeof(struct ospf_packet) - 8,
				  (pkt + 1),
				  ntohs(pkt->length) -
				  sizeof(struct ospf_packet), NULL);
}

/**
 * ospf_rx_hook
 * @sk: socket we received the packet. Its ignored.
 * @size: size of the packet
 *
 * This is the entry point for messages from neighbors. Many checks (like
 * authentication, checksums, size) are done before the packet is passed to
 * non generic functions.
 */
int
ospf_rx_hook(sock * sk, int size)
{
  struct ospf_packet *ps;
  struct ospf_iface *ifa = (struct ospf_iface *) (sk->data);
  struct proto *p = (struct proto *) (ifa->proto);
  struct ospf_neighbor *n;
  char *mesg = "Bad OSPF packet from ";

  if (ifa->stub)
    return (1);

  DBG("%s: RX_Hook called on interface %s.\n", p->name, sk->iface->name);

  ps = (struct ospf_packet *) ipv4_skip_header(sk->rbuf, &size);
  if (ps == NULL)
  {
    log(L_ERR "%s%I - bad IP header", mesg, sk->faddr);
    return 1;
  }

  if ((unsigned) size < sizeof(struct ospf_packet))
  {
    log(L_ERR "%s%I - too short (%u bytes)", mesg, sk->faddr, size);
    return 1;
  }

  if ((ntohs(ps->length) != size) || (size != (4 * (size / 4))))
  {
    log(L_ERR "%s%I - size field does not match", mesg, sk->faddr);
    return 1;
  }

  if (ps->version != OSPF_VERSION)
  {
    log(L_ERR "%s%I - version %u", mesg, sk->faddr, ps->version);
    return 1;
  }

  if (!ipsum_verify(ps, 16, (void *) ps + sizeof(struct ospf_packet),
		    ntohs(ps->length) - sizeof(struct ospf_packet), NULL))
  {
    log(L_ERR "%s%I - bad checksum", mesg, sk->faddr);
    return 1;
  }

  if (!ospf_rx_authenticate(ifa, ps))
  {
    log(L_ERR "%s%I - bad password", mesg, sk->faddr);
    return 1;
  }

  if (ntohl(ps->areaid) != ifa->an)
  {
    log(L_ERR "%s%I - other area %ld", mesg, sk->faddr, ps->areaid);
    return 1;
  }

  if (ntohl(ps->routerid) == p->cf->global->router_id)
  {
    log(L_ERR "%s%I - received my own router ID!", mesg, sk->faddr);
    return 1;
  }

  if (ntohl(ps->routerid) == 0)
  {
    log(L_ERR "%s%I - router id = 0.0.0.0", mesg, sk->faddr);
    return 1;
  }

  if ((unsigned) size > ifa->iface->mtu)
  {
    log(L_ERR "%s%I - received larger packet than MTU", mesg, sk->faddr);
    return 1;
  }

  n = find_neigh(ifa, ntohl(((struct ospf_packet *) ps)->routerid));

  if(!n && (ps->type != HELLO_P))
  {
    OSPF_TRACE(D_PACKETS, "Received non-hello packet from uknown neighbor (%I)", sk->faddr);
    return 1;
  }

  /* Dump packet 
     pu8=(u8 *)(sk->rbuf+5*4);
     for(i=0;i<ntohs(ps->length);i+=4)
     DBG("%s: received %u,%u,%u,%u\n",p->name, pu8[i+0], pu8[i+1], pu8[i+2],
     pu8[i+3]);
     DBG("%s: received size: %u\n",p->name,size);
   */

  switch (ps->type)
  {
  case HELLO_P:
    DBG("%s: Hello received.\n", p->name);
    ospf_hello_receive((struct ospf_hello_packet *) ps, ifa, n, sk->faddr);
    break;
  case DBDES_P:
    DBG("%s: Database description received.\n", p->name);
    ospf_dbdes_receive((struct ospf_dbdes_packet *) ps, ifa, n);
    break;
  case LSREQ_P:
    DBG("%s: Link state request received.\n", p->name);
    ospf_lsreq_receive((struct ospf_lsreq_packet *) ps, ifa, n);
    break;
  case LSUPD_P:
    DBG("%s: Link state update received.\n", p->name);
    ospf_lsupd_receive((struct ospf_lsupd_packet *) ps, ifa, n);
    break;
  case LSACK_P:
    DBG("%s: Link state ack received.\n", p->name);
    ospf_lsack_receive((struct ospf_lsack_packet *) ps, ifa, n);
    break;
  default:
    log(L_ERR "%s%I - wrong type %u", mesg, sk->faddr, ps->type);
    return 1;
  };
  return 1;
}

void
ospf_tx_hook(sock * sk)
{
  struct ospf_iface *ifa;
  struct proto *p;

  ifa = (struct ospf_iface *) (sk->data);

  p = (struct proto *) (ifa->proto);
  DBG("%s: TX_Hook called on interface %s\n", p->name, sk->iface->name);
}

void
ospf_err_hook(sock * sk, int err UNUSED)
{
  struct ospf_iface *ifa;
  struct proto *p;

  ifa = (struct ospf_iface *) (sk->data);

  p = (struct proto *) (ifa->proto);
  DBG("%s: Err_Hook called on interface %s\n", p->name, sk->iface->name);
}

void
sk_send_to_agt(sock * sk, u16 len, struct ospf_iface *ifa, u8 state)
{
  struct ospf_neighbor *n;

  WALK_LIST(NODE n, ifa->neigh_list) if (n->state >= state)
    sk_send_to(sk, len, n->ip, OSPF_PROTO);
}

void
sk_send_to_bdr(sock * sk, u16 len, struct ospf_iface *ifa)
{
  if (ipa_compare(ifa->drip, ipa_from_u32(0)) != 0)
    sk_send_to(sk, len, ifa->drip, OSPF_PROTO);
  if (ipa_compare(ifa->bdrip, ipa_from_u32(0)) != 0)
    sk_send_to(sk, len, ifa->bdrip, OSPF_PROTO);
}
