/*
 *	BIRD -- OSPF
 *
 *	(c) 1999 Ondrej Filip <feela@network.cz>
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
    n->inactim=tm_new(p->pool);
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
restart_waittim(struct ospf_iface *ifa)
{
  tm_start(ifa->wait_timer,ifa->waitint);
}

void
ospf_hello_rx(struct ospf_hello_packet *ps, struct proto *p,
  struct ospf_iface *ifa, int size, ip_addr faddr)
{
  char sip[100]; /* FIXME: Should be smaller */
  u32 nrid, *pnrid;
  struct ospf_neighbor *neigh,*n;
  u8 i,twoway;

  nrid=ntohl(((struct ospf_packet *)ps)->routerid);

  if((unsigned)ipa_mklen(ipa_ntoh(ps->netmask))!=ifa->iface->addr->pxlen)
  {
    ip_ntop(ps->netmask,sip);
    log("%s: Bad OSPF packet from %u received: bad netmask %s.",
      p->name, nrid, sip);
    log("%s: Discarding",p->name);
    return;
  }
  
  if(ntohs(ps->helloint)!=ifa->helloint)
  {
    log("%s: Bad OSPF packet from %u received: hello interval mismatch.",
      p->name, nrid);
    log("%s: Discarding",p->name);
    return;
  }

  if(ntohl(ps->deadint)!=ifa->helloint*ifa->deadc)
  {
    log("%s: Bad OSPF packet from %u received: dead interval mismatch.",
      p->name, nrid);
    log("%s: Discarding",p->name);
    return;
  }

  if(ps->options!=ifa->options)
  {
    log("%s: Bad OSPF packet from %u received: options mismatch.",
      p->name, nrid);	/* FIXME: This not good */
    log("%s: Discarding",p->name);
    return;
  }

  if((n=find_neigh(ifa, nrid))==NULL)
  {
    log("%s: New neighbor found: %u.", p->name,nrid);
    n=mb_alloc(p->pool, sizeof(struct ospf_neighbor));
    add_tail(&ifa->neigh_list, NODE n);
    n->rid=nrid;
    n->ip=faddr;
    n->dr=ntohl(ps->dr);
    n->bdr=ntohl(ps->bdr);
    n->priority=ps->priority;
    n->options=ps->options;
    n->ifa=ifa;
    n->adj=0;
    n->ldbdes=mb_alloc(p->pool, ifa->iface->mtu);
    neigh_chstate(n,NEIGHBOR_DOWN);
    install_inactim(n);
    n->rxmt_timer=tm_new(p->pool);
    n->rxmt_timer->data=n;
    n->rxmt_timer->randomize=0;
    n->rxmt_timer->hook=rxmt_timer_hook;
    n->rxmt_timer->recurrent=ifa->rxmtint;
    DBG("%s: Installing rxmt timer.\n", p->name);
    n->lsrr_timer=tm_new(p->pool);
    n->lsrr_timer->data=n;
    n->lsrr_timer->randomize=0;
    n->lsrr_timer->hook=lsrr_timer_hook;
    n->lsrr_timer->recurrent=ifa->rxmtint;
    DBG("%s: Installing lsrr timer.\n", p->name);
  }
  ospf_neigh_sm(n, INM_HELLOREC);

  pnrid=(u32 *)((struct ospf_hello_packet *)(ps+1));

  twoway=0;
  for(i=0;i<size-(sizeof(struct ospf_hello_packet));i++)
  {
    if(ntohl(*(pnrid+i))==p->cf->global->router_id)
    {
      DBG("%s: Twoway received. %u\n", p->name, nrid);
      ospf_neigh_sm(n, INM_2WAYREC);
      twoway=1;
      break;
    }
  }

  if(!twoway) ospf_neigh_sm(n, INM_1WAYREC);

  /* Check priority change */
  if(n->priority!=(n->priority=ps->priority))
  {
    ospf_int_sm(ifa, ISM_NEICH);
  }

  /* Check neighbor's designed router idea */
  if((n->rid!=ntohl(ps->dr)) && (ntohl(ps->bdr)==0) &&
    (n->state>=NEIGHBOR_2WAY))
  {
    ospf_int_sm(ifa, ISM_BACKS);
  }
  if((n->rid==ntohl(ps->dr)) && (n->dr!=ntohl(ps->dr)))
  {
    ospf_int_sm(ifa, ISM_NEICH);
  }
  if((n->rid==n->dr) && (n->dr!=ntohl(ps->dr)))
  {
    ospf_int_sm(ifa, ISM_NEICH);
  }
  n->dr=ntohl(ps->dr);	/* And update it */

  /* Check neighbor's backup designed router idea */
  if((n->rid==ntohl(ps->bdr)) && (n->state>=NEIGHBOR_2WAY))
  {
    ospf_int_sm(ifa, ISM_BACKS);
  }
  if((n->rid==ntohl(ps->bdr)) && (n->bdr!=ntohl(ps->bdr)))
  {
    ospf_int_sm(ifa, ISM_NEICH);
  }
  if((n->rid==n->bdr) && (n->bdr!=ntohl(ps->bdr)))
  {
    ospf_int_sm(ifa, ISM_NEICH);
  }
  n->bdr=ntohl(ps->bdr);	/* And update it */

  ospf_neigh_sm(n, INM_HELLOREC);
}

void
hello_timer_hook(timer *timer)
{
  struct ospf_iface *ifa;
  struct ospf_hello_packet *pkt;
  struct ospf_packet *op;
  struct proto *p;
  struct ospf_neighbor *neigh;
  u16 length;
  u32 *pp;
  u8 i;

  ifa=(struct ospf_iface *)timer->data;
  p=(struct proto *)(ifa->proto);
  debug("%s: Hello timer fired on interface %s.\n",
    p->name, ifa->iface->name);
  /* Now we should send a hello packet */
  /* First a common packet header */
  if(ifa->type!=OSPF_IT_NBMA)
  {
    /* Now fill ospf_hello header */
    pkt=(struct ospf_hello_packet *)(ifa->hello_sk->tbuf);
    op=(struct ospf_packet *)pkt;

    fill_ospf_pkt_hdr(ifa, pkt, HELLO);

    pkt->netmask=ipa_mkmask(ifa->iface->addr->pxlen);
    ipa_hton(pkt->netmask);
    pkt->helloint=ntohs(ifa->helloint);
    pkt->options=ifa->options;
    pkt->priority=ifa->priority;
    pkt->deadint=htonl(ifa->deadc*ifa->helloint);
    pkt->dr=htonl(ifa->drid);
    pkt->bdr=htonl(ifa->bdrid);

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
    sk_send(ifa->hello_sk,length);
  }
}

void
wait_timer_hook(timer *timer)
{
  struct ospf_iface *ifa;
  struct proto *p;

  ifa=(struct ospf_iface *)timer->data;
  p=(struct proto *)(ifa->proto);
  debug("%s: Wait timer fired on interface %s.\n",
    p->name, ifa->iface->name);
  ospf_int_sm(ifa, ISM_WAITF);
}

/* Neighbor is inactive for a long time. Remove it. */
void
neighbor_timer_hook(timer *timer)
{
  struct ospf_neighbor *n;
  struct ospf_iface *ifa;
  struct proto *p;

  n=(struct ospf_neighbor *)timer->data;
  ifa=n->ifa;
  p=(struct proto *)(ifa->proto);
  debug("%s: Inactivity timer fired on interface %s for neighbor %u.\n",
    p->name, ifa->iface->name, n->rid);
  tm_stop(n->inactim);
  rfree(n->inactim);
  rem_node(NODE n);
  mb_free(n);
  debug("%s: Deleting neigbor.\n", p->name);
  /* FIXME: Go on */
}
