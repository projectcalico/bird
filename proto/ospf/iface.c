/*
 *	BIRD -- OSPF
 *
 *	(c) 1999 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

char *ospf_is[]={ "down", "loop", "waiting", "point-to-point", "drother",
  "backup", "dr" };

char *ospf_ism[]={ "interface up", "wait timer fired", "backup seen",
  "neighbor change", "loop indicated", "unloop indicated", "interface down"};   

char *ospf_it[]={ "broadcast", "nbma", "point-to-point", "virtual link" };

void
iface_chstate(struct ospf_iface *ifa, u8 state)
{
  struct proto_ospf *po=ifa->proto;
  struct proto *p=&po->proto;
  u8 oldstate;

  if(ifa->state!=state)
  {
    debug("%s: Changing state of iface: %s from \"%s\" into \"%s\".\n",
      p->name, ifa->iface->name, ospf_is[ifa->state], ospf_is[state]);
    oldstate=ifa->state;
    ifa->state=state;
    if(ifa->iface->flags & IF_MULTICAST)
    {
      if((state==OSPF_IS_BACKUP)||(state==OSPF_IS_DR))
      {
        if(ifa->dr_sk==NULL)
        {
          DBG("%s: Adding new multicast socket for (B)DR\n", p->name);
          ifa->dr_sk=sk_new(p->pool);
          ifa->dr_sk->type=SK_IP_MC;
	  ifa->dr_sk->dport=OSPF_PROTO;
          ifa->dr_sk->saddr=AllDRouters;
          ifa->dr_sk->daddr=AllDRouters;
          ifa->dr_sk->tos=IP_PREC_INTERNET_CONTROL;
          ifa->dr_sk->ttl=1;
          ifa->dr_sk->rx_hook=ospf_rx_hook;
          ifa->dr_sk->tx_hook=ospf_tx_hook;
          ifa->dr_sk->err_hook=ospf_err_hook;
          ifa->dr_sk->iface=ifa->iface;
          ifa->dr_sk->rbsize=ifa->iface->mtu;
          ifa->dr_sk->tbsize=ifa->iface->mtu;
          ifa->dr_sk->data=(void *)ifa;
          if(sk_open(ifa->dr_sk)!=0)
	  {
	    DBG("%s: SK_OPEN: new? mc open failed.\n", p->name);
	  }
        }
      }
      else
      {
        if(ifa->dr_sk!=NULL)
	{
	  rfree(ifa->dr_sk);
	  ifa->dr_sk=NULL;
	}
      }
      if((oldstate==OSPF_IS_DR)&&(state>=OSPF_IS_WAITING))
      {
        net_flush_lsa(ifa->nlsa,po,ifa->oa);
        if(can_flush_lsa(ifa->oa)) flush_lsa(ifa->nlsa,ifa->oa);
        ifa->nlsa=NULL;
      }
    }
  }
}

void
downint(struct ospf_iface *ifa)
{
  struct ospf_neighbor *n,*nx;
  struct proto *p=&ifa->proto->proto;
  struct proto_ospf *po=ifa->proto;

  WALK_LIST_DELSAFE(n,nx,ifa->neigh_list)
  {
    debug("%s: Removing neighbor %I\n", p->name, n->ip);
    ospf_neigh_remove(n);
  }
  rem_node(NODE ifa);
  if(ifa->hello_sk!=NULL)
  {
    rfree(ifa->hello_sk);
  }
  if(ifa->dr_sk!=NULL)
  {
    rfree(ifa->dr_sk);
  }
  if(ifa->ip_sk!=NULL)
  {
    rfree(ifa->ip_sk);
  }
  if(ifa->wait_timer!=NULL)
  {
    tm_stop(ifa->wait_timer);
    rfree(ifa->wait_timer);
  }
  if(ifa->hello_timer!=NULL)
  {
    tm_stop(ifa->hello_timer);
    rfree(ifa->hello_timer);
  }
  mb_free(ifa);
}

void
ospf_int_sm(struct ospf_iface *ifa, int event)
{
  struct proto *p=(struct proto *)(ifa->proto);
  struct proto_ospf *po=ifa->proto;
  struct ospf_area *oa=ifa->oa;

  debug("%s: SM on iface %s. Event is \"%s\".\n",
    p->name, ifa->iface->name, ospf_ism[event]);

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
      schedule_rt_lsa(ifa->oa);
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
        schedule_rt_lsa(ifa->oa);
      }
      break;
    case ISM_DOWN:
      iface_chstate(ifa, OSPF_IS_DOWN);
      downint(ifa);
      schedule_rt_lsa(oa);
      break;
    case ISM_LOOP:	/* Useless? */
      iface_chstate(ifa, OSPF_IS_LOOP);
      downint(ifa);
      schedule_rt_lsa(ifa->oa);
      break;
    case ISM_UNLOOP:
      iface_chstate(ifa, OSPF_IS_DOWN);
      schedule_rt_lsa(ifa->oa);
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

  ifa->wait_timer=tm_new(pool);
  ifa->wait_timer->data=ifa;
  ifa->wait_timer->randomize=0;
  ifa->wait_timer->hook=wait_timer_hook;
  ifa->wait_timer->recurrent=0;
  if(ifa->waitint==0) ifa->waitint= WAIT_DMH*ifa->helloint;
  DBG("%s: Installing wait timer. (%u)\n", p->name, ifa->waitint);

  if(ifa->rxmtint==0) ifa->rxmtint=RXMTINT_D;
}

void
ospf_iface_default(struct ospf_iface *ifa)
{
  u8 i;

  ifa->oa=NULL;
  ifa->an=0;		/* FIXME This should respect config */
  ifa->cost=COST_D;
  ifa->rxmtint=RXMTINT_D;
  ifa->inftransdelay=INFTRANSDELAY_D;
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
    ifa=mb_allocz(p->pool, sizeof(struct ospf_iface));
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
	log("%s: Ignoring this interface.", p->name);
	return;
      }
      ifa->dr_sk=NULL;

      if((ifa->ip_sk=ospf_open_ip_socket(ifa))==NULL)
      {
        log("%s: Huh? could not open ip socket on interface %s?", p->name,
          iface->name);
	mb_free(ifa);
	log("%s: Ignoring this interface", p->name);
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
    ifa->nlsa=NULL;
    ifa->fadj=0;
    ospf_int_sm(ifa, ISM_UP);
  }

  if(flags & IF_CHANGE_DOWN)
  {
    if((ifa=find_iface((struct proto_ospf *)p, iface))!=NULL)
    {
      debug("%s: killing interface %s.\n", p->name, iface->name);
      ospf_int_sm(ifa, ISM_DOWN);
    }
  }

  if(flags & IF_CHANGE_MTU)
  {
    if((ifa=find_iface((struct proto_ospf *)p, iface))!=NULL)
    {
      debug("%s: changing MTU on interface %s.\n", p->name, iface->name);
      /* FIXME: change MTU */
    }
  }
}

void
ospf_iface_info(struct ospf_iface *ifa)
{
  int x;
  cli_msg(-1015,"Interface \"%s\":", ifa->iface->name);
  cli_msg(-1015,"\tArea: %I (%u)", ifa->oa->areaid, ifa->oa->areaid);
  cli_msg(-1015,"\tType: %s", ospf_it[ifa->type]);
  cli_msg(-1015,"\tState: %s", ospf_is[ifa->state]);
  cli_msg(-1015,"\tPriority: %u", ifa->priority);
  cli_msg(-1015,"\tCost: %u", ifa->cost);
  cli_msg(-1015,"\tHello timer: %u", ifa->helloint);
  cli_msg(-1015,"\tWait timer: %u", ifa->waitint);
  cli_msg(-1015,"\tDead timer: %u", ifa->deadc*ifa->helloint);
  cli_msg(-1015,"\tRetransmit timer: %u", ifa->rxmtint);
  cli_msg(-1015,"\tDesigned router (ID): %I", ifa->drid);
  cli_msg(-1015,"\tDesigned router (IP): %I", ifa->drip);
  cli_msg(-1015,"\tBackup designed router (ID): %I", ifa->bdrid);
  cli_msg(-1015,"\tBackup designed router (IP): %I", ifa->bdrip);
}
