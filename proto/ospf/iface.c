/*
 *	BIRD -- OSPF
 *
 *	(c) 1999 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

void
iface_chstate(struct ospf_iface *ifa, u8 state)
{
  struct proto *p;

  p=(struct proto *)(ifa->proto);
  debug("%s: Changing state of iface: %s from %u into %u.\n",
    p->name, ifa->iface->name, ifa->state, state);
  ifa->state=state;
}

void
downint(struct ospf_iface *ifa)
{
  /* FIXME: Delete all neighbors! */
}

void
ospf_int_sm(struct ospf_iface *ifa, int event)
{
  struct proto *p;

  p=(struct proto *)(ifa->proto);

  DBG("%s: SM on iface %s. Event is %d.\n",
    p->name, ifa->iface->name, event);

  switch(event)
  {
    case ISM_UP:
      if(ifa->state==OSPF_IS_DOWN)
      {
        /* Now, nothing should be adjacent */
        restart_hellotim(ifa);
        if((ifa->type==OSPF_IT_PTP) || (ifa->type==OSPF_IT_VLINK))
        {
          iface_chstate(ifa, OSPF_IS_PTP);
        }
        else
        {
          if(ifa->priority==0)
          {
            iface_chstate(ifa, OSPF_IS_DROTHER);
          } 
          else
          {
             iface_chstate(ifa, OSPF_IS_WAITING);
             restart_waittim(ifa);
          }
        }
	addifa_rtlsa(ifa);
      }
      break;
    case ISM_BACKS:
    case ISM_WAITF:
      if(ifa->state==OSPF_IS_WAITING)
      {
        bdr_election(ifa ,p);
      }
      break;
    case ISM_NEICH:
      if((ifa->state==OSPF_IS_DROTHER) || (ifa->state==OSPF_IS_DR) ||
        (ifa->state==OSPF_IS_BACKUP))
      {
        bdr_election(ifa ,p);
      }
    case ISM_DOWN:
      iface_chstate(ifa, OSPF_IS_DOWN);
      downint(ifa);
      break;
    case ISM_LOOP:	/* Useless? */
      iface_chstate(ifa, OSPF_IS_LOOP);
      downint(ifa);
      break;
    case ISM_UNLOOP:
      iface_chstate(ifa, OSPF_IS_DOWN);
      break;
    default:
      die("%s: ISM - Unknown event?",p->name);
      break;
  }
	
}

sock *
ospf_open_mc_socket(struct ospf_iface *ifa)
{
  sock *mcsk;
  struct proto *p;

  p=(struct proto *)(ifa->proto);


  /* FIXME: No NBMA networks now */

  if(ifa->iface->flags & IF_MULTICAST)
  {
    mcsk=sk_new(p->pool);
    mcsk->type=SK_IP_MC;
    mcsk->dport=OSPF_PROTO;
    mcsk->saddr=AllSPFRouters;
    mcsk->daddr=AllSPFRouters;
    mcsk->tos=IP_PREC_INTERNET_CONTROL;
    mcsk->ttl=1;
    mcsk->rx_hook=ospf_rx_hook;
    mcsk->tx_hook=ospf_tx_hook;
    mcsk->err_hook=ospf_err_hook;
    mcsk->iface=ifa->iface;
    mcsk->rbsize=ifa->iface->mtu;
    mcsk->tbsize=ifa->iface->mtu;
    mcsk->data=(void *)ifa;
    if(sk_open(mcsk)!=0)
    {
      DBG("%s: SK_OPEN: mc open failed.\n",p->name);
      return(NULL);
    }
    DBG("%s: SK_OPEN: mc opened.\n",p->name);
    return(mcsk);
  }
  else return(NULL);
}

sock *
ospf_open_ip_socket(struct ospf_iface *ifa)
{
  sock *ipsk;
  struct proto *p;

  p=(struct proto *)(ifa->proto);

  ipsk=sk_new(p->pool);
  ipsk->type=SK_IP;
  ipsk->dport=OSPF_PROTO;
  ipsk->saddr=ifa->iface->addr->ip;
  ipsk->tos=IP_PREC_INTERNET_CONTROL;
  ipsk->ttl=1;
  ipsk->rx_hook=ospf_rx_hook;
  ipsk->tx_hook=ospf_tx_hook;
  ipsk->err_hook=ospf_err_hook;
  ipsk->iface=ifa->iface;
  ipsk->rbsize=ifa->iface->mtu;
  ipsk->tbsize=ifa->iface->mtu;
  ipsk->data=(void *)ifa;
  if(sk_open(ipsk)!=0)
  {
    DBG("%s: SK_OPEN: ip open failed.\n",p->name);
    return(NULL);
  }
  DBG("%s: SK_OPEN: ip opened.\n",p->name);
  return(ipsk);
}

/* 
 * This will later decide, wheter use iface for OSPF or not
 * depending on config
 */
u8
is_good_iface(struct proto *p, struct iface *iface)
{
  if(iface->flags & IF_UP)
  {
    if(!(iface->flags & IF_IGNORE)) return 1;
  }
  return 0;
}

/* Of course, it's NOT true now */
u8
ospf_iface_clasify(struct iface *ifa, struct proto *p)
{
  /* FIXME: Latter I'll use config - this is incorrect */
  DBG("%s: Iface flags=%x.\n", p->name, ifa->flags);
  if((ifa->flags & (IF_MULTIACCESS|IF_MULTICAST))==
    (IF_MULTIACCESS|IF_MULTICAST))
  {
     DBG("%s: Clasifying BCAST.\n", p->name);
     return OSPF_IT_BCAST;
  }
  if((ifa->flags & (IF_MULTIACCESS|IF_MULTICAST))==
    IF_MULTIACCESS)
  {
    DBG("%s: Clasifying NBMA.\n", p->name);
    return OSPF_IT_NBMA;
  }
  DBG("%s: Clasifying P-T-P.\n", p->name);
  return OSPF_IT_PTP;
}

void
ospf_add_timers(struct ospf_iface *ifa, pool *pool)
{
  struct proto *p;

  p=(struct proto *)(ifa->proto);
  /* Add hello timer */
  ifa->hello_timer=tm_new(pool);
  ifa->hello_timer->data=ifa;
  ifa->hello_timer->randomize=0;
  if(ifa->helloint==0) ifa->helloint=HELLOINT_D;
  ifa->hello_timer->hook=hello_timer_hook;
  ifa->hello_timer->recurrent=ifa->helloint;
  DBG("%s: Installing hello timer. (%u)\n", p->name, ifa->helloint);

  ifa->rxmt_timer=tm_new(pool);
  ifa->rxmt_timer->data=ifa;
  ifa->rxmt_timer->randomize=0;
  if(ifa->rxmtint==0) ifa->rxmtint=RXMTINT_D;
  ifa->rxmt_timer->hook=rxmt_timer_hook;
  ifa->rxmt_timer->recurrent=ifa->rxmtint;
  DBG("%s: Installing rxmt timer. (%u)\n", p->name, ifa->rxmtint);

  ifa->wait_timer=tm_new(pool);
  ifa->wait_timer->data=ifa;
  ifa->wait_timer->randomize=0;
  ifa->wait_timer->hook=wait_timer_hook;
  ifa->wait_timer->recurrent=0;
  if(ifa->waitint==0) ifa->waitint= WAIT_DMH*ifa->helloint;
  DBG("%s: Installing wait timer. (%u)\n", p->name, ifa->waitint);
}

void
ospf_iface_default(struct ospf_iface *ifa)
{
  u8 i;

  ifa->area=0; /* FIXME: Read from config */
  ifa->cost=COST_D;
  ifa->rxmtint=RXMTINT_D;
  ifa->iftransdelay=IFTRANSDELAY_D;
  ifa->priority=PRIORITY_D;
  ifa->helloint=HELLOINT_D;
  ifa->deadc=DEADC_D;
  ifa->autype=0;
  for(i=0;i<8;i++) ifa->aukey[i]=0;
  ifa->options=2;
  ifa->drip=ipa_from_u32(0x00000000);
  ifa->drid=0;
  ifa->bdrip=ipa_from_u32(0x00000000);
  ifa->bdrid=0;
  ifa->type=ospf_iface_clasify(ifa->iface, (struct proto *)ifa->proto);
}

struct ospf_iface*
find_iface(struct proto_ospf *p, struct iface *what)
{
  struct ospf_iface *i;

  WALK_LIST (i, p->iface_list)
    if ((i)->iface == what)
      return i;
  return NULL;
}

void
ospf_if_notify(struct proto *p, unsigned flags, struct iface *iface)
{
  struct ospf_iface *ifa;
  sock *mcsk;

  struct ospf_config *c;
  c=(struct ospf_config *)(p->cf);

  DBG("%s: If notify called\n", p->name);
  if (iface->flags & IF_IGNORE)
    return;

  if((flags & IF_CHANGE_UP) && is_good_iface(p, iface))
  {
    debug("%s: using interface %s.\n", p->name, iface->name);
    /* FIXME: Latter I'll use config - this is incorrect */
    ifa=mb_alloc(p->pool, sizeof(struct ospf_iface));
    ifa->proto=(struct proto_ospf *)p;
    ifa->iface=iface;
    ospf_iface_default(ifa);
    if(ifa->type!=OSPF_IT_NBMA)
    {
      if((ifa->hello_sk=ospf_open_mc_socket(ifa))==NULL)
      {
        log("%s: Huh? could not open mc socket on interface %s?", p->name,
          iface->name);
	mb_free(ifa);
	log("%s: Ignoring this interface\n", p->name);
	return;
      }

      if((ifa->ip_sk=ospf_open_ip_socket(ifa))==NULL)
      {
        log("%s: Huh? could not open ip socket on interface %s?", p->name,
          iface->name);
	mb_free(ifa);
	log("%s: Ignoring this interface\n", p->name);
	return;
      }

      init_list(&(ifa->neigh_list));
    }
    /* FIXME: This should read config */
    ifa->helloint=0;
    ifa->waitint=0;
    ospf_add_timers(ifa,p->pool);
    add_tail(&((struct proto_ospf *)p)->iface_list, NODE ifa);
    ifa->state=OSPF_IS_DOWN;
    ospf_int_sm(ifa, ISM_UP);
  }

  if(flags & IF_CHANGE_DOWN)
  {
    if((ifa=find_iface((struct proto_ospf *)p, iface))!=NULL)
    {
      debug(" OSPF: killing interface %s.\n", iface->name);
      ospf_int_sm(ifa, ISM_DOWN);
    }
  }

  if(flags & IF_CHANGE_MTU)
  {
    if((ifa=find_iface((struct proto_ospf *)p, iface))!=NULL)
    {
      debug(" OSPF: changing MTU on interface %s.\n", iface->name);
      /* FIXME: change MTU */
    }
  }
}

