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

#include "ospf.h"

int
ospf_rx_hook(sock *sk, int size)
{
  DBG(" OSPF: RX_Hook called on interface ");
  DBG(sk->iface->name);
  DBG(".\n");
  return(1);
}

void
ospf_tx_hook(sock *sk)
{
  DBG(" OSPF: TX_Hook called on interface ");
  DBG(sk->iface->name);
  DBG(".\n");
}

void
ospf_err_hook(sock *sk, int err)
{
  DBG(" OSPF: Err_Hook called on interface ");
  DBG(sk->iface->name);
  DBG(".\n");
}

/* This will change ! */
sock *
ospf_open_socket(struct proto *p, struct ospf_iface *ifa)
{
  sock *mcsk;

  /* No NBMA networks now */

  if(ifa->iface->flags & IF_MULTICAST)
  {
    mcsk=sk_new(p->pool);
    mcsk->type=SK_IP_MC;
    mcsk->dport=OSPF_PROTO;
    mcsk->saddr=AllSPFRouters;
    mcsk->daddr=AllSPFRouters;
    mcsk->ttl=1;
    mcsk->rx_hook=ospf_rx_hook;
    mcsk->tx_hook=ospf_tx_hook;
    mcsk->err_hook=ospf_err_hook;
    mcsk->iface=ifa->iface;
    mcsk->rbsize=ifa->iface->mtu;
    mcsk->tbsize=ifa->iface->mtu;
    if(sk_open(mcsk)!=0)
    {
      DBG(" OSPF: SK_OPEN: failed\n");
      return(NULL);
    }
    DBG(" OSPF: SK_OPEN: open\n");
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
hello_timer_hook(timer *timer)
{
  struct ospf_iface *ifa;

  ifa=(struct ospf_iface *)timer->data;
  debug(" OSPF: Hello timer fired on interface %s.\n",
    ifa->iface->name);
}

void
add_hello_timer(struct ospf_iface *ifa)
{
  if(ifa->helloint==0) ifa->helloint=HELLOINT_D;
  
  ifa->timer->hook=hello_timer_hook;
  ifa->timer->recurrent=ifa->helloint;
  ifa->timer->expires=0;
  tm_start(ifa->timer,0);
  DBG(" OSPF: Installing hello timer.\n");
}

void
wait_timer_hook(timer *timer)
{
  struct ospf_iface *ifa;

  ifa=(struct ospf_iface *)timer->data;
  debug(" OSPF: Wait timer fired on interface %s.\n",
    ifa->iface->name);
  if(ifa->state=OSPF_IS_WAITING)
  {
    /*
     * Wait time fired. Now we must change state
     * to DR or DROTHER depending on priority
     */
    if(ifa->priority!=0)
    {
      debug(" OSPF: Changing state into DR.\n");
      ifa->state=OSPF_IS_DR;
      ifa->drip=ifa->iface->ip;
      /* FIXME: Set ifa->drid */
    }
    else
    {
      debug(" OSPF: Changing state into DROTHER.\n");
      ifa->state=OSPF_IS_DROTHER;
    }
    add_hello_timer(ifa);
  }
}

void
add_wait_timer(struct ospf_iface *ifa,pool *pool, int wait)
{
  ifa->timer=tm_new(pool);
  ifa->timer->data=ifa;
  ifa->timer->randomize=1;
  if((ifa->type!=OSPF_IT_PTP))
  {
    ifa->timer->hook=wait_timer_hook;
    ifa->timer->recurrent=0;
    ifa->timer->expires=0;
    tm_start(ifa->timer,(wait!=0 ? wait : WAIT_D));
    DBG(" OSPF: Installing wait timer.\n");
  }
  else
  {
    add_hello_timer(ifa);
  }
}

void
ospf_iface_default(struct ospf_iface *ifa)
{
  int i;

  ifa->area=0;
  ifa->cost=COST_D;
  ifa->rxmtint=RXMTINT_D;
  ifa->iftransdelay=IFTRANSDELAY_D;
  ifa->priority=PRIORITY_D;
  ifa->helloint=HELLOINT_D;
  ifa->deadint=DEADINT_D;
  ifa->autype=0;
  for(i=0;i<8;i++) ifa->aukey[i]=0;
  ifa->options=0;
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
ospf_if_notify(struct proto *p, unsigned flags, struct iface *new, struct iface *old)
{
  struct ospf_iface *ifa;
  sock *mcsk, *newsk;
  struct ospf_sock *osk;

  struct ospf_config *c;
  c=(struct ospf_config *)(p->cf);

  DBG(" OSPF: If notify called\n");

  if((flags & IF_CHANGE_UP) && is_good_iface(p, new))
  {
    debug(" OSPF: using interface %s.\n", new->name);
    /* FIXME: Latter I'll use config - this is incorrect */
    ifa=mb_alloc(p->pool, sizeof(struct ospf_iface));
    ifa->iface=new;
    add_tail(&((struct proto_ospf *)p)->iface_list, NODE ifa);
    ospf_iface_default(ifa);
    /* FIXME: This should read config */
    add_wait_timer(ifa,p->pool,0);
    init_list(&(ifa->sk_list));
    if((mcsk=ospf_open_socket(p, ifa))!=NULL)
    {
      osk=(struct ospf_sock *)mb_alloc(p->pool, sizeof(struct ospf_sock));
      osk->sk=mcsk;
      add_tail(&(ifa->sk_list),NODE osk);
    }
  }

  if(flags & IF_CHANGE_DOWN)
  {
    if((ifa=find_iface((struct proto_ospf *)p, old))!=NULL)
    {
      debug(" OSPF: killing interface %s.\n", old->name);
    }
  }

  if(flags & IF_CHANGE_MTU)
  {
    if((ifa=find_iface((struct proto_ospf *)p, old))!=NULL)
    {
      debug(" OSPF: changing MTU on interface %s.\n", old->name);
    }
  }
}


static int
ospf_start(struct proto *p)
{
  DBG(" OSPF: Start\n");

  p->if_notify=ospf_if_notify;

  return PS_UP;
}

static void
ospf_dump(struct proto *p)
{
  char areastr[20];
  struct ospf_config *c = (void *) p->cf;

  DBG(" OSPF: Dump.\n");
  debug(" -AreaID: %d\n", c->area );
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

