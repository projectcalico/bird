/*
 *	BIRD -- OSPF
 *
 *	(c) 1999 - 2000 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

void
install_inactim(struct ospf_neighbor *n)
{
  struct proto *p;
  struct ospf_iface *ifa;

  ifa=n->ifa;
  p=(struct proto *)(ifa->proto);

  if(n->inactim==NULL)
  {
    n->inactim=tm_new(n->pool);
    n->inactim->data=n;
    n->inactim->randomize=0;
    n->inactim->hook=neighbor_timer_hook;
    n->inactim->recurrent=0;
    DBG("%s: Installing inactivity timer.\n", p->name);
  }
}

void
restart_inactim(struct ospf_neighbor *n)
{
  tm_start(n->inactim,n->ifa->deadc*n->ifa->helloint);
}

void
restart_hellotim(struct ospf_iface *ifa)
{
  tm_start(ifa->hello_timer,ifa->helloint);
}

void
restart_polltim(struct ospf_iface *ifa)
{
  if(ifa->poll_timer)
    tm_start(ifa->poll_timer,ifa->pollint);
}

void
restart_waittim(struct ospf_iface *ifa)
{
  tm_start(ifa->wait_timer,ifa->waitint);
}

void
ospf_hello_rx(struct ospf_hello_packet *ps, struct proto *p,
  struct ospf_iface *ifa, int size, ip_addr faddr)
{
  u32 nrid, *pnrid;
  struct ospf_neighbor *neigh,*n;
  u8 i,twoway,oldpriority;
  ip_addr olddr,oldbdr;
  ip_addr mask;
  char *beg=": Bad OSPF hello packet from ", *rec=" received: ";
  int eligible=0;
  pool *pool;

  nrid=ntohl(((struct ospf_packet *)ps)->routerid);

  OSPF_TRACE(D_PACKETS, "Received hello from %I via %s",faddr,ifa->iface->name);
  mask=ps->netmask;
  ipa_ntoh(mask);

  if((unsigned)ipa_mklen(mask)!=ifa->iface->addr->pxlen)
  {
    log("%s%s%I%sbad netmask %I.", p->name, beg, nrid, rec,
      mask);
    return;
  }
  
  if(ntohs(ps->helloint)!=ifa->helloint)
  {
    log("%s%s%I%shello interval mismatch.", p->name, beg, faddr, rec);
    return;
  }

  if(ntohl(ps->deadint)!=ifa->helloint*ifa->deadc)
  {
    log("%s%s%I%sdead interval mismatch.", p->name, beg, faddr, rec);
    return;
  }

  if(ps->options!=ifa->options)
  {
    log("%s%s%I%soptions mismatch.", p->name, beg, faddr, rec);
    return;
  }

  if((n=find_neigh(ifa, nrid))==NULL)
  {
    if((ifa->type==OSPF_IT_NBMA))
    {
      struct nbma_node *nn;
      int found=0;

      WALK_LIST(nn,ifa->nbma_list)
      {
        if(ipa_compare(faddr,nn->ip)==0)
	{
	  found=1;
	  break;
	}
      }
      if((found==0)&&(ifa->strictnbma))
      {
        log("%s: Ignoring new neighbor: %I on %s.", p->name, faddr,
          ifa->iface->name);
	return;
      }
      if(found)
      {
        eligible=nn->eligible;
        if(((ps->priority==0)&&eligible)||((ps->priority>0)&&(eligible==0)))
        {
          log("%s: Eligibility mismatch for neighbor: %I on %s", p->name,
            faddr, ifa->iface->name);
	  return;
        }
      }
    }
    OSPF_TRACE(D_EVENTS, "New neighbor found: %I on %s.", faddr,
      ifa->iface->name);
    pool=rp_new(p->pool, "OSPF Neighbor");
    n=mb_allocz(pool, sizeof(struct ospf_neighbor));
    n->pool=pool;
    add_tail(&ifa->neigh_list, NODE n);
    n->rid=nrid;
    n->ip=faddr;
    n->dr=ps->dr;
    ipa_ntoh(n->dr);
    n->bdr=ps->bdr;
    ipa_ntoh(n->bdr);
    n->priority=ps->priority;
    n->options=ps->options;
    n->ifa=ifa;
    n->adj=0;
    n->ldbdes=mb_alloc(pool, ifa->iface->mtu);
    n->state=NEIGHBOR_DOWN;
    install_inactim(n);
    n->rxmt_timer=tm_new(pool);
    n->rxmt_timer->data=n;
    n->rxmt_timer->randomize=0;
    n->rxmt_timer->hook=rxmt_timer_hook;
    n->rxmt_timer->recurrent=ifa->rxmtint;
    DBG("%s: Installing rxmt timer.\n", p->name);
    n->lsrr_timer=tm_new(pool);
    n->lsrr_timer->data=n;
    n->lsrr_timer->randomize=0;
    n->lsrr_timer->hook=lsrr_timer_hook;
    n->lsrr_timer->recurrent=ifa->rxmtint;
    DBG("%s: Installing lsrr timer.\n", p->name);
    init_list(&n->ackl);
    n->ackd_timer=tm_new(pool);
    n->ackd_timer->data=n;
    n->ackd_timer->randomize=0;
    n->ackd_timer->hook=ackd_timer_hook;
    n->ackd_timer->recurrent=ifa->rxmtint/2;
    DBG("%s: Installing ackd timer.\n", p->name);
  }
  ospf_neigh_sm(n, INM_HELLOREC);

  pnrid=(u32 *)((struct ospf_hello_packet *)(ps+1));

  twoway=0;
  for(i=0;i<size-(sizeof(struct ospf_hello_packet));i++)
  {
    if(ntohl(*(pnrid+i))==p->cf->global->router_id)
    {
      DBG("%s: Twoway received from %I\n", p->name, faddr);
      ospf_neigh_sm(n, INM_2WAYREC);
      twoway=1;
      break;
    }
  }

  if(!twoway) ospf_neigh_sm(n, INM_1WAYREC);

  olddr = n->dr;
  n->dr = ipa_ntoh(ps->dr);
  oldbdr = n->bdr;
  n->bdr = ipa_ntoh(ps->bdr);
  oldpriority = n->priority;
  n->priority = ps->priority;

  /* Check priority change */
  if(n->state>=NEIGHBOR_2WAY)
  {
    if(n->priority!=oldpriority) ospf_int_sm(ifa, ISM_NEICH);

    /* Router is declaring itself ad DR and there is no BDR */
    if((ipa_compare(n->ip,n->dr)==0) && (ipa_to_u32(n->bdr)==0)
      && (n->state!=NEIGHBOR_FULL))
      ospf_int_sm(ifa, ISM_BACKS);

    /* Neighbor is declaring itself as BDR */
    if((ipa_compare(n->ip,n->bdr)==0) && (n->state!=NEIGHBOR_FULL))
      ospf_int_sm(ifa, ISM_BACKS);

    /* Neighbor is newly declaring itself as DR or BDR */
    if(((ipa_compare(n->ip,n->dr)==0) && (ipa_compare(n->dr,olddr)!=0))
      || ((ipa_compare(n->ip,n->bdr)==0) && (ipa_compare(n->bdr,oldbdr)!=0)))
      ospf_int_sm(ifa, ISM_NEICH);

    /* Neighbor is no more declaring itself as DR or BDR */
    if(((ipa_compare(n->ip,olddr)==0) && (ipa_compare(n->dr,olddr)!=0))
      || ((ipa_compare(n->ip,oldbdr)==0) && (ipa_compare(n->bdr,oldbdr)!=0)))
      ospf_int_sm(ifa, ISM_NEICH);
  }

  if(ifa->type!=OSPF_IT_NBMA)
  {
    if((ifa->priority==0)&&(n->priority>0)) hello_send(NULL,0, n);
  }
  ospf_neigh_sm(n, INM_HELLOREC);
}

void
poll_timer_hook(timer *timer)
{
  hello_send(timer,1, NULL);
}

void
hello_timer_hook(timer *timer)
{
  hello_send(timer,0, NULL);
}

void
hello_send(timer *timer,int poll, struct ospf_neighbor *dirn)
{
  struct ospf_iface *ifa;
  struct ospf_hello_packet *pkt;
  struct ospf_packet *op;
  struct proto *p;
  struct ospf_neighbor *neigh;
  u16 length;
  u32 *pp;
  u8 i;

  if(timer==NULL) ifa=dirn->ifa;
  else ifa=(struct ospf_iface *)timer->data;

  if(ifa->stub) return;		/* Don't send any packet on stub iface */

  p=(struct proto *)(ifa->proto);
  DBG("%s: Hello/Poll timer fired on interface %s.\n",
    p->name, ifa->iface->name);
  /* Now we should send a hello packet */
  /* First a common packet header */
  if(ifa->type!=OSPF_IT_NBMA)
  {
    pkt=(struct ospf_hello_packet *)(ifa->hello_sk->tbuf);
  }
  else 
  {
    pkt=(struct ospf_hello_packet *)(ifa->ip_sk->tbuf);
  }

  /* Now fill ospf_hello header */
  op=(struct ospf_packet *)pkt;

  fill_ospf_pkt_hdr(ifa, pkt, HELLO_P);

  pkt->netmask=ipa_mkmask(ifa->iface->addr->pxlen);
  ipa_hton(pkt->netmask);
  pkt->helloint=ntohs(ifa->helloint);
  pkt->options=ifa->options;
  pkt->priority=ifa->priority;
  pkt->deadint=htonl(ifa->deadc*ifa->helloint);
  pkt->dr=ifa->drip;
  ipa_hton(pkt->dr);
  pkt->bdr=ifa->bdrip;
  ipa_hton(pkt->bdr);

  /* Fill all neighbors */
  i=0;
  pp=(u32 *)(((u8 *)pkt)+sizeof(struct ospf_hello_packet));
  WALK_LIST (neigh, ifa->neigh_list)
  {
    *(pp+i)=htonl(neigh->rid);
    i++;
  }

  length=sizeof(struct ospf_hello_packet)+i*sizeof(u32);
  op->length=htons(length);

  ospf_pkt_finalize(ifa, op);

    /* And finally send it :-) */
  if(ifa->type!=OSPF_IT_NBMA)
  {
    sk_send(ifa->hello_sk,length);
  }
  else	/* NBMA */
  {
    struct ospf_neighbor *n1;
    struct nbma_node *nb;
    int send;

    if(timer==NULL)	/* Response to received hello */
    {
      sk_send_to(ifa->ip_sk, length, dirn->ip, OSPF_PROTO);
    }
    else
    {
      int toall=0;
      int meeli=0;
      if(ifa->state>OSPF_IS_DROTHER) toall=1;
      if(ifa->priority>0) meeli=1;

      WALK_LIST(nb,ifa->nbma_list)
      {
        send=1;
        WALK_LIST(n1, ifa->neigh_list)
        {
          if(ipa_compare(nb->ip,n1->ip)==0)
	  {
	    send=0;
	    break;
          }
        }
        if((poll==1)&&(send))
	{
          if(toall||(meeli&&nb->eligible))
            sk_send_to(ifa->ip_sk, length, nb->ip, OSPF_PROTO);
	}
      }
      if(poll==0)
      {
        WALK_LIST(n1,ifa->neigh_list)
        {
          if(toall||(n1->rid==ifa->drid)||(n1->rid==ifa->bdrid)||
            (meeli&&(n1->priority>0)))
            sk_send_to(ifa->ip_sk, length, n1->ip, OSPF_PROTO);
        }
      }
    }
  }
  OSPF_TRACE(D_PACKETS, "Hello sent via %s",ifa->iface->name);
}

void
wait_timer_hook(timer *timer)
{
  struct ospf_iface *ifa;
  struct proto *p;

  ifa=(struct ospf_iface *)timer->data;
  p=(struct proto *)(ifa->proto);
  OSPF_TRACE(D_EVENTS, "Wait timer fired on interface %s.",
    ifa->iface->name);
  ospf_int_sm(ifa, ISM_WAITF);
}

