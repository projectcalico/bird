/*
 *	BIRD -- OSPF
 *
 *	(c) 1999 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include <string.h>

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/protocol.h"
#include "nest/route.h"
#include "conf/conf.h"
#include "lib/ip.h"
#include "lib/socket.h"
#include "lib/lists.h"
#include "lib/timer.h"
#include "lib/checksum.h"

#include "ospf.h"

/* Try to build neighbor adjacency (if does not exists) */
void
tryadj(struct ospf_neighbor *n, struct proto *p)
{
  if(n->state==NEIGHBOR_INIT)
  {
    DBG("%s: Going to build adjacency.\n", p->name);
    n->state=NEIGHBOR_EXSTART;
    /* FIXME Go on */
  }
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
}

void
iface_chstate(struct ospf_iface *ifa, u8 state)
{
  struct proto *p;

  p=(struct proto *)(ifa->proto);
  debug("%s: Changing state of iface: %s from %u into %u.\n",
    p->name, ifa->iface->name, ifa->state, state);
  ifa->state=state;
}

struct ospf_neighbor *
electbdr(list nl)
{
  struct ospf_neighbor *neigh,*n1,*n2;

  n1=NULL;
  n2=NULL;
  WALK_LIST (neigh, nl)		/* First try those decl. themselves */
  {
    if(neigh->state>=NEIGHBOR_2WAY)	/* Higher than 2WAY */
      if(neigh->priority>0)		/* Eligible */
        if(neigh->rid!=neigh->dr)	/* And not declaring itself DR */
	{
	  if(neigh->rid==neigh->bdr)	/* Declaring BDR */
          {
            if(n1!=NULL)
            {
              if(neigh->priority>n1->priority) n1=neigh;
	      else if(neigh->priority==n1->priority)
	          if(neigh->rid>n1->rid) n1=neigh;
            }
	    else
            {
              n1=neigh;
            }
          }
	  else				/* And NOT declaring BDR */
          {
            if(n2!=NULL)
            {
              if(neigh->priority>n2->priority) n2=neigh;
	      else if(neigh->priority==n2->priority)
	          if(neigh->rid>n2->rid) n2=neigh;
            }
	    else
            {
              n2=neigh;
            }
          }
      }
  }
  if(n1==NULL) n1=n2;

  return(n1);
}

struct ospf_neighbor *
electdr(list nl)
{
  struct ospf_neighbor *neigh,*n;

  n=NULL;
  WALK_LIST (neigh, nl)	/* And now DR */
  {
    if(neigh->state>=NEIGHBOR_2WAY)	/* Higher than 2WAY */
      if(neigh->priority>0)		/* Eligible */
        if(neigh->rid==neigh->dr)	/* And declaring itself DR */
	{
          if(n!=NULL)
          {
            if(neigh->priority>n->priority) n=neigh;
	    else if(neigh->priority==n->priority)
	        if(neigh->rid>n->rid) n=neigh;
          }
	  else
          {
            n=neigh;
          }
      }
  }

  return(n);
}

void
backupseen(struct ospf_iface *ifa)
{
  struct proto *p;
  struct ospf_neighbor *neigh,*ndr,*nbdr,me;
  u32 myid;

  p=(struct proto *)(ifa->proto);

  tm_stop(ifa->wait_timer);
  DBG("%s: Stoping wait timer\n",p->name);

  DBG("%s: (B)DR election.\n",p->name);

  myid=p->cf->global->router_id;

  me.state=NEIGHBOR_2WAY;
  me.rid=myid;
  me.priority=ifa->priority;
  me.dr=ifa->drid;
  me.bdr=ifa->bdrid;

  add_tail(&ifa->neigh_list, NODE &me);

  nbdr=electbdr(ifa->neigh_list);
  ndr=electdr(ifa->neigh_list);
 
  if(ndr==NULL) ndr=nbdr;

  if(((ifa->drid==myid) && (ndr->rid!=myid))
    || ((ifa->drid!=myid) && (ndr->rid==myid))
    || ((ifa->bdrid==myid) && (nbdr->rid!=myid)) 
    || ((ifa->bdrid!=myid) && (nbdr->rid==myid)))
  {
    if(ndr==NULL) ifa->drid=me.dr=0;
    else ifa->drid=me.dr=ndr->rid;

    if(nbdr==NULL) ifa->bdrid=me.bdr=0;
    else ifa->bdrid=me.bdr=nbdr->rid;

    nbdr=electbdr(ifa->neigh_list);
    ndr=electdr(ifa->neigh_list);
  }
  if(ndr==NULL) ifa->drid=0;
  else ifa->drid=ndr->rid;

  if(nbdr==NULL) ifa->bdrid=0;
  else ifa->bdrid=me.bdr=nbdr->rid;

  DBG("%s: DR=%u, BDR=%u\n",p->name, ifa->drid, ifa->bdrid);

  if(myid==ifa->drid) iface_chstate(ifa, OSPF_IS_DR);
  else
  {
    if(myid==ifa->bdrid) iface_chstate(ifa, OSPF_IS_BACKUP);
    else iface_chstate(ifa, OSPF_IS_DROTHER);
  }

  rem_node(NODE &me);
}

void
ospf_hello_rx(struct ospf_hello_packet *ps, struct proto *p,
  struct ospf_iface *ifa, int size)
{
  char sip[100]; /* FIXME: Should be smaller */
  u32 nrid, *pnrid;
  struct ospf_neighbor *neigh,*n;
  int i,twoway;

  nrid=ntohl(((struct ospf_packet *)ps)->routerid);

  if(ipa_mklen(ipa_ntoh(ps->netmask))!=ifa->iface->addr->pxlen)
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

  n=NULL;
  WALK_LIST (neigh, ifa->neigh_list)
  {
    if(neigh->rid==nrid)
    {
      n=neigh;
      break;
    }
  }

  if(n==NULL)
  {
    log("%s: New neighbor found: %u.",p->name,nrid);
    n=mb_alloc(p->pool, sizeof(struct ospf_neighbor));
    add_tail(&ifa->neigh_list, NODE n);
    n->inactim=tm_new(p->pool);
    n->inactim->data=n;
    n->inactim->randomize=0;
    n->inactim->hook=neighbor_timer_hook;
    n->inactim->recurrent=0;
    DBG("%s: Installing inactivity timer.\n", p->name);
    n->state=NEIGHBOR_INIT;
    n->rid=nrid;
    n->dr=ntohl(ps->dr);
    n->bdr=ntohl(ps->bdr);
    n->priority=ps->priority;
    n->options=ps->options;
  }
  tm_start(n->inactim,ifa->deadc*ifa->helloint);

  twoway=0;
  pnrid=(u32 *)((struct ospf_hello_packet *)(ps+1));

  for(i=0;i<size-(sizeof(struct ospf_hello_packet));i++)
  {
    if(ntohl(*(pnrid+i))==p->cf->global->router_id)
    {
      twoway=1;
      DBG("%s: Twoway received. %u\n", p->name, nrid);
      break;
    }
  }

  if(twoway)
  {
    if(n->state<NEIGHBOR_2WAY) n->state=NEIGHBOR_2WAY;
  }
  else
  {
    if(n->state>=NEIGHBOR_2WAY)
    {
      /* FIXME Delete all learnt */
      n->state=NEIGHBOR_INIT;
    }
  }

  /* Check priority change */
  if(n->priority!=(n->priority=ps->priority))
  {
    /* FIXME NeighborChange */;
  }

  /* Check neighbor's designed router idea */
  if((n->rid!=ntohl(ps->dr)) && (ntohl(ps->bdr)==0) &&
    (ifa->state==OSPF_IS_WAITING) && (n->state>=NEIGHBOR_2WAY))
  {
    backupseen(ifa);
  }
  if((n->rid==ntohl(ps->dr)) && (n->dr!=ntohl(ps->dr)))
  {
    /* FIXME NeighborChange */;
  }
  if((n->rid==n->dr) && (n->dr!=ntohl(ps->dr)))
  {
    /* FIXME NeighborChange */;
  }
  n->dr=ntohl(ps->dr);	/* And update it */

  /* Check neighbor's backup designed router idea */
  if((n->rid==ntohl(ps->bdr)) && (ifa->state==OSPF_IS_WAITING)
    && (n->state>=NEIGHBOR_2WAY))
  {
    backupseen(ifa);
  }
  if((n->rid==ntohl(ps->bdr)) && (n->bdr!=ntohl(ps->bdr)))
  {
    /* FIXME NeighborChange */;
  }
  if((n->rid==n->bdr) && (n->bdr!=ntohl(ps->bdr)))
  {
    /* FIXME NeighborChange */;
  }
  n->bdr=ntohl(ps->bdr);	/* And update it */


  switch(ifa->state)
  {
    case OSPF_IS_DOWN:
      die("%s: Iface %s in down state?", p->name, ifa->iface->name);
      break;
    case OSPF_IS_WAITING:
      DBG(p->name);
      DBG(": Neighbor? on iface ");
      DBG(ifa->iface->name);
      DBG("\n");
      break;
    case OSPF_IS_DROTHER:
      if(twoway)
      {
        if((n->rid==n->dr) || (n->rid==n->bdr)) tryadj(n,p);
	else n->state=NEIGHBOR_2WAY;
      }
      else
      {
	if(n->state==NEIGHBOR_2WAY) n->state=NEIGHBOR_INIT;
	if(n->state>NEIGHBOR_2WAY) 
        {
          /* FIXME Kill adjacency */;
          n->state=NEIGHBOR_INIT;
        }
      }
      break;
    case OSPF_IS_PTP:
    case OSPF_IS_BACKUP:
    case OSPF_IS_DR:
      if(twoway) tryadj(n,p);
      else
      {
	if(n->state==NEIGHBOR_2WAY) n->state=NEIGHBOR_INIT;
	if(n->state>NEIGHBOR_2WAY) 
        {
          /* FIXME Kill adjacency */;
          n->state=NEIGHBOR_INIT;
        }
      }
      break;
    default:
      die("%s: Iface %s in unknown state?",p->name, ifa->iface->name);
      break;
  }
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
  DBG(p->name);
  DBG(": RX_Hook called on interface ");
  DBG(sk->iface->name);
  DBG(".\n");

  ps = (struct ospf_packet *) ipv4_skip_header(sk->rbuf, &size);
  if(ps==NULL)
  {
    log("%s: Bad OSPF packet received: bad IP header", p->name);
    log("%s: Discarding",p->name);
    return(1);
  }
  
  if(size < sizeof(struct ospf_packet))
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

  if(ps->areaid!=ifa->area)
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
    debug("%s: received %u,%u,%u,%u\n",p->name, pu8[i+0], pu8[i+1], pu8[i+2],
		    pu8[i+3]);
  debug("%s: received size: %u\n",p->name,size);

  switch(ps->type)
  {
    case HELLO:
      DBG(p->name);
      DBG(": Hello received.\n");
      ospf_hello_rx((struct ospf_hello_packet *)ps, p, ifa, size);
      break;
    case DBDES:
      break;
    case LSREQ:
      break;
    case LSUPD:
      break;
    case LSACK:
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

/* This will change ! */
sock *
ospf_open_socket(struct proto *p, struct ospf_iface *ifa)
{
  sock *mcsk;

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
      DBG(p->name);
      DBG(": SK_OPEN: failed\n");
      return(NULL);
    }
    DBG(p->name);
    DBG(": SK_OPEN: open\n");
    return(mcsk);
  }
  else return(NULL);
}

/* 
 * This will later decide, wheter use iface for OSPF or not
 * depending on config
 */
int
is_good_iface(struct proto *p, struct iface *iface)
{
  if(iface->flags & IF_UP)
  {
    if(!(iface->flags & IF_IGNORE)) return 1;
  }
  return 0;
}

/* Of course, it's NOT true now */
int
ospf_iface_clasify(struct iface *ifa)
{
  /* FIXME: Latter I'll use config - this is incorrect */
  if((ifa->flags & (IF_MULTIACCESS|IF_MULTICAST))==
    (IF_MULTIACCESS|IF_MULTICAST))
  {
     DBG(" OSPF: Clasifying BROADCAST.\n");
     return OSPF_IT_BROADCAST;
  }
  if((ifa->flags & (IF_MULTIACCESS|IF_MULTICAST))==
    IF_MULTIACCESS)
  {
    DBG(" OSPF: Clasifying NBMA.\n");
    return OSPF_IT_NBMA;
  }
  DBG(" OSPF: Clasifying P-T-P.\n");
  return OSPF_IT_PTP;
}

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
  pkt->areaid=htonl(ifa->area);
  pkt->autype=htons(ifa->autype);
  pkt->checksum=0;
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
  int i;

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
    pp=(u32 *)(((byte *)pkt)+sizeof(struct ospf_hello_packet));
    WALK_LIST (neigh, ifa->neigh_list)
    {
      *(pp+i)=htonl(neigh->rid);
      i++;
    }

    length=sizeof(struct ospf_hello_packet)+i*sizeof(u32);

    op->length=ntohs(length);

    /* FIXME Do authentification */

    /* Count checksum */
    op->checksum=ipsum_calculate(op,sizeof(struct ospf_packet)-8,
      &(pkt->netmask),length-sizeof(struct ospf_packet),NULL);

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
  if(ifa->state=OSPF_IS_WAITING) backupseen(ifa);
  else die("%s: Wait timer fired I'm not in WAITING state?", p->name);
}

void
ospf_add_timers(struct ospf_iface *ifa, pool *pool, int wait)
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
  tm_start(ifa->hello_timer,ifa->helloint);
  DBG("%s: Installing hello timer. (%u)\n", p->name, ifa->helloint);
  if((ifa->type!=OSPF_IT_PTP))
  {
    /* Install wait timer on NOT-PtP interfaces */
    ifa->wait_timer=tm_new(pool);
    ifa->wait_timer->data=ifa;
    ifa->wait_timer->randomize=0;
    ifa->wait_timer->hook=wait_timer_hook;
    ifa->wait_timer->recurrent=0;
    ifa->state=OSPF_IS_WAITING;
    tm_start(ifa->wait_timer,(wait!=0 ? wait : WAIT_DMH*ifa->helloint));
    DBG("%s: Installing wait timer. (%u)\n", p->name, (wait!=0 ? wait : WAIT_DMH*ifa->helloint));
  }
  else ifa->state=OSPF_IS_PTP;
}

void
ospf_iface_default(struct ospf_iface *ifa)
{
  int i;

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
  ifa->type=ospf_iface_clasify(ifa->iface);
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

  DBG(" OSPF: If notify called\n");
  if (iface->flags & IF_IGNORE)
    return;

  if((flags & IF_CHANGE_UP) && is_good_iface(p, iface))
  {
    debug(" OSPF: using interface %s.\n", iface->name);
    /* FIXME: Latter I'll use config - this is incorrect */
    ifa=mb_alloc(p->pool, sizeof(struct ospf_iface));
    ifa->proto=(struct proto_ospf *)p;
    ifa->iface=iface;
    ospf_iface_default(ifa);
    if(ifa->type!=OSPF_IT_NBMA)
    {
      if((mcsk=ospf_open_socket(p, ifa))!=NULL)
      {
	ifa->hello_sk=mcsk;
      }
      else
      {
        log("%s: Huh? could not open socket on interface %s?", p->name,
          iface->name);
	mb_free(ifa);
	log("%s: Ignoring this interface\n", p->name);
	return;
      }
      /* FIXME: In fail case??? */
      init_list(&(ifa->neigh_list));
    }
    /* FIXME: NBMA? */
    /* FIXME: This should read config */
    ifa->helloint=0;
    ospf_add_timers(ifa,p->pool,0);
    add_tail(&((struct proto_ospf *)p)->iface_list, NODE ifa);
  }

  if(flags & IF_CHANGE_DOWN)
  {
    if((ifa=find_iface((struct proto_ospf *)p, iface))!=NULL)
    {
      debug(" OSPF: killing interface %s.\n", iface->name);
      /* FIXME: This should delete ifaces */
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


static int
ospf_start(struct proto *p)
{
  DBG(p->name);
  DBG(": Start\n");

  p->if_notify=ospf_if_notify;

  return PS_UP;
}

static void
ospf_dump(struct proto *p)
{
  char areastr[20];
  struct ospf_config *c = (void *) p->cf;

  DBG(p->name);
  DBG(": Dump.\n");
  debug(" -AreaID: %u\n", c->area );
}

static struct proto *
ospf_init(struct proto_config *c)
{
  struct proto *p = proto_new(c, sizeof(struct proto_ospf));

  DBG(" OSPF: Init.\n");
  p->neigh_notify = NULL;
  p->if_notify = NULL;
  init_list(&((struct proto_ospf *)p)->iface_list);
  return p;
}

static void
ospf_preconfig(struct protocol *p, struct config *c)
{
  DBG( " OSPF: preconfig\n" );
}

static void
ospf_postconfig(struct proto_config *c)
{
  DBG( " OSPF: postconfig\n" );
}

struct protocol proto_ospf = {
  name:		"OSPF",
  init:		ospf_init,
  dump:		ospf_dump,
  start:	ospf_start,
  preconfig:	ospf_preconfig,
  postconfig:	ospf_postconfig,
};

