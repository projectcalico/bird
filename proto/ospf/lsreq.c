/*
 *	BIRD -- OSPF
 *
 *	(c) 2000 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

void
ospf_lsreq_tx(struct ospf_neighbor *n)
{
  snode *sn;
  struct top_hash_entry *en;
  struct ospf_lsreq_packet *pk;
  struct ospf_packet *op;
  struct ospf_lsreq_header *lsh;
  u16 length;
  int i,j;

  pk=(struct ospf_lsreq_packet *)n->ifa->ip_sk->tbuf;
  op=(struct ospf_packet *)n->ifa->ip_sk->tbuf;

  fill_ospf_pkt_hdr(n->ifa, pk, LSREQ);

  sn=SHEAD(n->lsrql);
  if(EMPTY_SLIST(n->lsrql))
  {
    if(n->state==NEIGHBOR_LOADING) ospf_neigh_sm(n, INM_LOADDONE);
    return;
  }
 
  i=j=(n->ifa->iface->mtu-SIPH-sizeof(struct ospf_lsreq_packet))/
    sizeof(struct ospf_lsreq_header);
  lsh=(struct ospf_lsreq_header *)(pk+1);
  
  for(;i>0;i--)
  {
    en=(struct top_hash_entry *)sn;
    lsh->padd1=0; lsh->padd2=0;
    lsh->type=en->lsa.type;
    lsh->rt=htonl(en->lsa.rt);
    lsh->id=htonl(en->lsa.id);
    DBG("Requesting %uth LSA: Type: %u, Id: %u, RT: %u\n",i, en->lsa.type,
		    en->lsa.id, en->lsa.rt);
    lsh++;
    if(sn==STAIL(n->lsrql)) break;
    sn=sn->next;
  }
  if(i!=0) i--;

  length=sizeof(struct ospf_lsreq_packet)+(j-i)*sizeof(struct ospf_lsreq_header);
  op->length=htons(length);
  ospf_pkt_finalize(n->ifa, op);
  sk_send_to(n->ifa->ip_sk,length, n->ip, OSPF_PROTO);
  DBG("Lsreq send to: %u\n", n->rid);
}

void
lsrr_timer_hook(timer *timer)
{
  struct ospf_iface *ifa;
  struct proto *p;
  struct ospf_neighbor *n;

  n=(struct ospf_neighbor *)timer->data;
  ifa=n->ifa;
  p=(struct proto *)(ifa->proto);
  debug("%s: LSRR timer fired on interface %s for neigh: %u.\n",
    p->name, ifa->iface->name, n->rid);
  ospf_lsreq_tx(n);
}

void
ospf_lsreq_rx(struct ospf_lsreq_packet *ps, struct proto *p,
  struct ospf_iface *ifa, u16 size)
{
  u32 nrid, myrid;
  struct ospf_neighbor *n;
  struct ospf_lsreq_header *lsh;
  struct l_lsr_head *llsh;
  list uplist;
  slab *upslab;
  int length;
  u8 i;

  nrid=ntohl(ps->ospf_packet.routerid);

  myrid=p->cf->global->router_id;

  if((n=find_neigh(ifa, nrid))==NULL)
  {
    debug("%s: Received lsreq from unknown neigbor! (%u)\n", p->name,
      nrid);
    return ;
  }
  if(n->state<NEIGHBOR_EXCHANGE) debug("%s: Ignoring it.\n", p->name);

  length=htons(ps->ospf_packet.length);
  lsh=(void *)(ps+1);
  init_list(&uplist);
  upslab=sl_new(p->pool,sizeof(struct l_lsr_head));

  for(i=0;i<(length-sizeof(struct ospf_lsreq_packet))/
    sizeof(struct ospf_lsreq_header);i++);
  {
    DBG("Processing LSA: ID=%u, Type=%u, Router=%u\n", ntohl(lsh->id),
    lsh->type, ntohl(lsh->rt));
    llsh=sl_alloc(upslab);
    llsh->lsh.id=ntohl(lsh->id);
    llsh->lsh.rt=ntohl(lsh->rt);
    llsh->lsh.type=lsh->type;
    add_tail(&uplist, NODE llsh);
    if(ospf_hash_find(n->ifa->oa->gr, llsh->lsh.id, llsh->lsh.rt,
      llsh->lsh.type)==NULL)
    {
      ospf_neigh_sm(n,INM_BADLSREQ);
      rfree(upslab);
      return;
    }
  }
  ospf_lsupd_tx_list(n, &uplist);
  rfree(upslab);
}

