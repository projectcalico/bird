/*
 *	BIRD -- OSPF
 *
 *	(c) 1999 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

void
fill_ospf_pkt_hdr(struct ospf_iface *ifa, void *buf, u8 h_type)
{
  struct ospf_packet *pkt;
  struct proto *p;
  
  p=(struct proto *)(ifa->proto);

  pkt=(struct ospf_packet *)buf;

  pkt->version=OSPF_VERSION;

  pkt->type=h_type;

  pkt->routerid=htonl(p->cf->global->router_id);
  pkt->areaid=htonl(ifa->an);
  pkt->autype=htons(ifa->autype);
  pkt->checksum=0;
}

void
ospf_tx_authenticate(struct ospf_iface *ifa, struct ospf_packet *pkt)
{
  /* FIXME */
  return;
}

void
ospf_pkt_finalize(struct ospf_iface *ifa, struct ospf_packet *pkt)
{

  ospf_tx_authenticate(ifa, pkt);

  /* Count checksum */
  pkt->checksum=ipsum_calculate(pkt,sizeof(struct ospf_packet)-8,
    (pkt+1),ntohs(pkt->length)-sizeof(struct ospf_packet),NULL);
}

int
ospf_rx_hook(sock *sk, int size)
{
#ifndef IPV6
  struct ospf_packet *ps;
  struct ospf_iface *ifa;
  struct proto *p;
  int i;
  u8 *pu8;


  ifa=(struct ospf_iface *)(sk->data);

  p=(struct proto *)(ifa->proto);
  DBG("%s: RX_Hook called on interface %s.\n",p->name, sk->iface->name);

  ps = (struct ospf_packet *) ipv4_skip_header(sk->rbuf, &size);
  if(ps==NULL)
  {
    log("%s: Bad OSPF packet received: bad IP header", p->name);
    log("%s: Discarding",p->name);
    return(1);
  }
  
  if((unsigned)size < sizeof(struct ospf_packet))
  {
    log("%s: Bad OSPF packet received: too short (%u bytes)", p->name, size);
    log("%s: Discarding",p->name);
    return(1);
  }

  if(ntohs(ps->length) != size)
  {
    log("%s: Bad OSPF packet received: size field does not match", p->name);
    log("%s: Discarding",p->name);
    return(1);
  }

  if(ps->version!=OSPF_VERSION)
  {
    log("%s: Bad OSPF packet received: version %u", p->name, ps->version);
    log("%s: Discarding",p->name);
    return(1);
  }

  if(!ipsum_verify(ps, 16,(void *)ps+sizeof(struct ospf_packet),
    ntohs(ps->length)-sizeof(struct ospf_packet), NULL))
  {
    log("%s: Bad OSPF packet received: bad checksum", p->name);
    log("%s: Discarding",p->name);
    return(1);
  }

  /* FIXME: Do authetification */

  if(ps->areaid!=ifa->an)
  {
    log("%s: Bad OSPF packet received: other area %ld", p->name, ps->areaid);
    log("%s: Discarding",p->name);
    return(1);
  }

  if(ntohl(ps->routerid)==p->cf->global->router_id)
  {
    log("%s: Bad OSPF packet received: received my own IP!.", p->name);
    log("%s: Discarding",p->name);
    return(1);
  }

  if(ntohl(ps->routerid)==0)
  {
    log("%s: Bad OSPF packet received: Id 0.0.0.0 is not allowed.", p->name);
    log("%s: Discarding",p->name);
    return(1);
  }
  
  /* Dump packet */
  pu8=(u8 *)(sk->rbuf+5*4);
  for(i=0;i<ntohs(ps->length);i+=4)
    DBG("%s: received %u,%u,%u,%u\n",p->name, pu8[i+0], pu8[i+1], pu8[i+2],
		    pu8[i+3]);
  debug("%s: received size: %u\n",p->name,size);

  switch(ps->type)
  {
    case HELLO:
      DBG("%s: Hello received.\n", p->name);
      ospf_hello_rx((struct ospf_hello_packet *)ps, p, ifa, size, sk->faddr);
      break;
    case DBDES:
      DBG("%s: Database description received.\n", p->name);
      ospf_dbdes_rx((struct ospf_dbdes_packet *)ps, p, ifa, size);
      break;
    case LSREQ:
      DBG("%s: Link state request received.\n", p->name);
      ospf_lsreq_rx((struct ospf_lsreq_packet *)ps, p, ifa, size);
      break;
    case LSUPD:
      DBG("%s: Link state update received.\n", p->name);
      break;
    case LSACK:
      DBG("%s: Link state ack received.\n", p->name);
      break;
    default:
      log("%s: Bad packet received: wrong type %u", p->name, ps->type);
      log("%s: Discarding",p->name);
      return(1);
  };
  DBG("\n");
#else
#error RX_Hook does not work for IPv6 now.
#endif
  return(1);
}

void
ospf_tx_hook(sock *sk)
{
  struct ospf_iface *ifa;
  struct proto *p;

  ifa=(struct ospf_iface *)(sk->data);

  p=(struct proto *)(ifa->proto);
  DBG("%s: TX_Hook called on interface %s\n", p->name,sk->iface->name);
}

void
ospf_err_hook(sock *sk, int err)
{
  struct ospf_iface *ifa;
  struct proto *p;

  ifa=(struct ospf_iface *)(sk->data);

  p=(struct proto *)(ifa->proto);
  DBG("%s: Err_Hook called on interface %s\n", p->name,sk->iface->name);
}

