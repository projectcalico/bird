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
  struct proto *p=&n->ifa->proto->proto;

  pk=(struct ospf_lsreq_packet *)n->ifa->ip_sk->tbuf;
  op=(struct ospf_packet *)n->ifa->ip_sk->tbuf;

  fill_ospf_pkt_hdr(n->ifa, pk, LSREQ_P);

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
    DBG("Requesting %uth LSA: Type: %u, Id: %I, RT: %I\n",i, en->lsa.type,
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
  debug("%s: LS request sent to: %I\n", p->name, n->rid);
}

void
lsrr_timer_hook(timer *timer)
{
  struct ospf_iface *ifa;
  struct proto *p;
  struct ospf_neighbor *n;
  struct top_hash_entry *en;

  n=(struct ospf_neighbor *)timer->data;
  ifa=n->ifa;
  p=(struct proto *)(ifa->proto);

  DBG("%s: LSRR timer fired on interface %s for neigh: %I.\n",
    p->name, ifa->iface->name, n->rid);
  if(n->state<NEIGHBOR_FULL) ospf_lsreq_tx(n);
  else
  {
    if(!EMPTY_SLIST(n->lsrtl))
    {
      list uplist;
      slab *upslab;
      struct l_lsr_head *llsh;

      init_list(&uplist);
      upslab=sl_new(p->pool,sizeof(struct l_lsr_head));

      WALK_SLIST(SNODE en,n->lsrtl)
      {
	if((SNODE en)->next==(SNODE en)) die("BUGGGGGG");
        llsh=sl_alloc(upslab);
        llsh->lsh.id=en->lsa.id;
        llsh->lsh.rt=en->lsa.rt;
        llsh->lsh.type=en->lsa.type;
	DBG("Working on ID: %I, RT: %I, Type: %u\n",
          en->lsa.id, en->lsa.rt, en->lsa.type);
        add_tail(&uplist, NODE llsh);
      }
      ospf_lsupd_tx_list(n, &uplist);
      rfree(upslab);
    }
  }
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
  int i,lsano;

  nrid=ntohl(ps->ospf_packet.routerid);

  myrid=p->cf->global->router_id;

  if((n=find_neigh(ifa, nrid))==NULL)
  {
    debug("%s: Received lsreq from unknown neighbor! (%I)\n", p->name,
      nrid);
    return ;
  }
  if(n->state<NEIGHBOR_EXCHANGE) return;

  debug("%s: Received LS req from neighbor: %I\n",p->name, n->ip);

  length=ntohs(ps->ospf_packet.length);
  lsh=(void *)(ps+1);
  init_list(&uplist);
  upslab=sl_new(p->pool,sizeof(struct l_lsr_head));

  lsano=(length-sizeof(struct ospf_lsreq_packet))/
    sizeof(struct ospf_lsreq_header);
  for(i=0;i<lsano;lsh++,i++)
  {
    DBG("Processing LSA: ID=%I, Type=%u, Router=%I\n", ntohl(lsh->id),
    lsh->type, ntohl(lsh->rt));
    llsh=sl_alloc(upslab);
    llsh->lsh.id=ntohl(lsh->id);
    llsh->lsh.rt=ntohl(lsh->rt);
    llsh->lsh.type=lsh->type;
    add_tail(&uplist, NODE llsh);
    if(ospf_hash_find(n->ifa->oa->gr, llsh->lsh.id, llsh->lsh.rt,
      llsh->lsh.type)==NULL)
    {
      log("Received bad LS req from: %I looking: RT: %I, ID: %I, Type: %u",
        n->ip, lsh->rt, lsh->id, lsh->type);
      ospf_neigh_sm(n,INM_BADLSREQ);
      rfree(upslab);
      return;
    }
  }
  ospf_lsupd_tx_list(n, &uplist);
  rfree(upslab);
}

