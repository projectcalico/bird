/*
 *	BIRD -- OSPF
 *
 *	(c) 2000 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

/* Note, that h is in network endianity! */
void
ospf_lsack_direct_tx(struct ospf_neighbor *n,struct ospf_lsa_header *h)
{
  struct ospf_packet *op;
  struct ospf_lsack_packet *pk;
  sock *sk=n->ifa->ip_sk;
  struct proto *p=&n->ifa->proto->proto;
  u16 len;

  DBG("Sending direct ACK to %I for Type: %u, ID: %I, RT: %I\n",n->rid,
    h->type, ntohl(h->id), ntohl(h->rt));

  pk=(struct ospf_lsack_packet *)sk->tbuf;
  op=(struct ospf_packet *)sk->tbuf;

  fill_ospf_pkt_hdr(n->ifa, pk, LSACK);

  memcpy(pk+1,h,sizeof(struct ospf_lsa_header));
  len=sizeof(struct ospf_lsack_packet)+sizeof(struct ospf_lsa_header);
  op->length=htons(len);
  ospf_pkt_finalize(n->ifa, op);
  sk_send_to(sk,len, n->ip, OSPF_PROTO);
  debug("%s: LS ack sent to %I\n", p->name, n->ip);
}

void
ospf_lsa_delay(struct ospf_neighbor *n,struct ospf_lsa_header *h,
  struct proto *p)
{
  struct lsah_n *no;

  no=mb_alloc(p->pool,sizeof(struct lsah_n));
  memcpy(&no->lsa,h,sizeof(struct ospf_lsa_header));
  add_tail(&n->ackl, NODE no);
  DBG("Adding delay ack for %I, ID: %I, RT: %I, Type: %u\n",n->rid,
    ntohl(h->id), ntohl(h->rt),h->type);
}

void
ackd_timer_hook(timer *t)
{
  struct ospf_neighbor *n=t->data;
  if(!EMPTY_LIST(n->ackl)) ospf_lsack_delay_tx(n);
}

void
ospf_lsack_delay_tx(struct ospf_neighbor *n)
{
  struct ospf_packet *op;
  struct ospf_lsack_packet *pk;
  sock *sk;
  u16 len,i=0;
  struct ospf_lsa_header *h;
  struct lsah_n *no;
  struct ospf_iface *ifa=n->ifa;

  DBG("Sending delay ack to %I\n", n->rid);

  if(ifa->type==OSPF_IT_BCAST)
  {
    sk=ifa->hello_sk;
  }
  else
  {
    sk=ifa->ip_sk;
  }

  pk=(struct ospf_lsack_packet *)sk->tbuf;
  op=(struct ospf_packet *)sk->tbuf;

  fill_ospf_pkt_hdr(n->ifa, pk, LSACK);
  h=(struct ospf_lsa_header *)(pk+1);

  while(!EMPTY_LIST(n->ackl))
  {
    no=(struct lsah_n *)HEAD(n->ackl);
    memcpy(h+i,&no->lsa, sizeof(struct ospf_lsa_header));
    i++;
    DBG("Iter %u ID: %I, RT: %I, Type: %u\n",i, ntohl((h+i)->id),
      ntohl((h+i)->rt),(h+i)->type);
    rem_node(NODE no);
    mb_free(no);
    if((i*sizeof(struct ospf_lsa_header)+sizeof(struct ospf_lsack_packet)+SIPH)>
      n->ifa->iface->mtu)
    {
      if(!EMPTY_LIST(n->ackl))
      {
        len=sizeof(struct ospf_lsack_packet)+i*sizeof(struct ospf_lsa_header);
	op->length=htons(len);
	ospf_pkt_finalize(n->ifa, op);
	DBG("Sending and continueing! Len=%u\n",len);
        if(ifa->type==OSPF_IT_BCAST)
	{
          if((ifa->state==OSPF_IS_DR)||(ifa->state==OSPF_IS_BACKUP))
	  {
	    sk_send_to(sk ,len, AllSPFRouters, OSPF_PROTO);
	  }
	  else
	  {
	    sk_send_to(sk ,len, AllDRouters, OSPF_PROTO);
	  }
	}
	else
	{
          sk_send_to_agt(sk, len, ifa, NEIGHBOR_EXCHANGE);
	}

	fill_ospf_pkt_hdr(n->ifa, pk, LSACK);
	i=0;
      }
    }
  }

  len=sizeof(struct ospf_lsack_packet)+i*sizeof(struct ospf_lsa_header);
  op->length=htons(len);
  ospf_pkt_finalize(n->ifa, op);
  DBG("Sending! Len=%u\n",len);
  if(ifa->type==OSPF_IT_BCAST)
  {
    if((ifa->state==OSPF_IS_DR)||(ifa->state==OSPF_IS_BACKUP))
    {
      sk_send_to(sk ,len, AllSPFRouters, OSPF_PROTO);
    }
    else
    {
      sk_send_to(sk ,len, AllDRouters, OSPF_PROTO);
    }
  }
  else
  {
    sk_send_to_agt(sk, len, ifa, NEIGHBOR_EXCHANGE);
  }
}



void
ospf_lsack_rx(struct ospf_lsack_packet *ps, struct proto *p,
  struct ospf_iface *ifa, u16 size)
{
  u32 nrid, myrid;
  struct ospf_neighbor *n;
  struct ospf_lsa_header lsa,*plsa;
  int length;
  u16 nolsa,i;
  struct top_hash_entry *en;

  nrid=ntohl(ps->ospf_packet.routerid);

  myrid=p->cf->global->router_id;

  if((n=find_neigh(ifa, nrid))==NULL)
  {
    debug("%s: Received LS ack from unknown neigbor! (%I)\n", p->name,
      nrid);
    return ;
  }

  if(n->state<NEIGHBOR_EXCHANGE) return;

  debug("%s: Received LS ack from %I\n", p->name,
      n->ip);

  nolsa=(ntohs(ps->ospf_packet.length)-sizeof(struct ospf_lsack_packet))/
    sizeof(struct ospf_lsa_header);
  DBG("Received %d lsa ack(s)\n",nolsa);
  plsa=( struct ospf_lsa_header *)(ps+1);

  for(i=0;i<nolsa;i++)
  {
    ntohlsah(plsa+i,&lsa);
    if((en=ospf_hash_find_header(n->lsrth,&lsa))==NULL) continue;

    if(lsa_comp(&lsa,&en->lsa)!=CMP_SAME)
    {
      log("Strange LS acknoledgement from %I",n->rid);
      log("Id: %I, Rt: %I, Type: %u",lsa.id, lsa.rt, lsa.type);
      log("I have: Age: %u, Seqno: %u", en->lsa.age, en->lsa.sn);
      log("He has: Age: %u, Seqno: %u", lsa.age, lsa.sn);
      continue;
    }

    DBG("Deleting LS Id: %I RT: %I Type: %u from LS Retl for neighbor %I\n",
      lsa.id,lsa.rt,lsa.type,n->rid);
    s_rem_node(SNODE en);
    ospf_hash_delete(n->lsrth,en);
  }  
}
