/*
 *	BIRD -- OSPF
 *
 *	(c) 2000 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

void
ospf_lsupd_tx(struct ospf_neighbor *n)
{
  /* FIXME Go on! */
}

void		/* I send all I received in LSREQ */
ospf_lsupd_tx_list(struct ospf_neighbor *n, list *l)
{
  struct l_lsr_head *llsh;
  u16 len;
  u32 lsano;
  struct top_hash_entry *en;
  struct ospf_lsupd_packet *pk;
  struct ospf_packet *op;
  void *pktpos;

  if(HEAD(*l)==NULL) return;

  pk=(struct ospf_lsupd_packet *)n->ifa->ip_sk->tbuf;
  op=(struct ospf_packet *)n->ifa->ip_sk->tbuf;
       
  fill_ospf_pkt_hdr(n->ifa, pk, LSUPD);
  len=SIPH+sizeof(struct ospf_lsupd_packet);
  lsano=0;
  pktpos=(pk+1);

  WALK_LIST(llsh, *l)
  {
    en=ospf_hash_find(n->ifa->oa->gr,llsh->lsh.id,llsh->lsh.rt,llsh->lsh.type);
    if((len+sizeof(struct ospf_lsa_header)+en->body_len)>n->ifa->iface->mtu)
    {
      pk->lsano=htonl(lsano);
      op->length=htons(len);
      ospf_pkt_finalize(n->ifa, op);
      sk_send_to(n->ifa->ip_sk,len, n->ip, OSPF_PROTO);

      fill_ospf_pkt_hdr(n->ifa, pk, LSUPD);
      len=SIPH+sizeof(struct ospf_lsupd_packet);
      lsano=0;
      pktpos=(pk+1);
    }
    htonlsah(&(en->lsa), pktpos);
    pktpos=pktpos+sizeof(struct ospf_lsa_header);
    htonlsab(en->lsa_body, pktpos, en->lsa.type, en->lsa.length);
    pktpos=pktpos+en->body_len;
    len=len+en->body_len+sizeof(struct ospf_lsa_header);
  }
  pk->lsano=htonl(lsano);
  op->length=htons(len);
  ospf_pkt_finalize(n->ifa, op);
  sk_send_to(n->ifa->ip_sk,len, n->ip, OSPF_PROTO);
}

void
ospf_lsupd_rx(struct ospf_lsupd_packet *ps, struct proto *p,
  struct ospf_iface *ifa, u16 size)
{
  u32 nrid, myrid;
  struct ospf_neighbor *n;
  struct ospf_lsreq_header *lsh;
  int length;
  u8 i;

  nrid=ntohl(ps->ospf_packet.routerid);

  myrid=p->cf->global->router_id;

  if((n=find_neigh(ifa, nrid))==NULL)
  {
    debug("%s: Received lsupd from unknown neigbor! (%u)\n", p->name,
      nrid);
    return ;
  }
  /* FIXME Go on! */
}

