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

#include "ospf.h"

int
ospf_rx_hook(sock *sk, int size)
{
  DBG(" RX_Hook_Called.\n");
  return(1);
}

void
ospf_tx_hook(sock *sk)
{
  DBG(" TX_Hook_Called.\n");
}

void
ospf_err_hook(sock *sk, int err)
{
  DBG(" Err_Hook_Called.\n");
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
byte
ospf_iface_clasify(struct iface *ifa)
{
  if((ifa->flags & (IF_MULTIACCESS|IF_MULTICAST))==
    IF_MULTIACCESS|IF_MULTICAST) return OSPF_IM_MULTICAST;
  if((ifa->flags & (IF_MULTIACCESS|IF_MULTICAST))==
    IF_MULTIACCESS) return OSPF_IM_NBMA;
  return OSPF_IM_PTP;
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
  ifa->dr=ipa_from_u32(0x00000000);
  ifa->bdr=ipa_from_u32(0x00000000);
  ifa->mode=ospf_iface_clasify(ifa->iface);
}

void
ospf_if_notify(struct proto *p, unsigned flags, struct iface *new, struct iface *old)
{
  struct ospf_iface *ospf_iface;

  struct ospf_config *c;
  c=(struct ospf_config *)(p->cf);

  

  DBG(" OSPF: If notify called\n");

  if(((flags & IF_CHANGE_UP)==IF_CHANGE_UP) && is_good_iface(p, new))
  {
    /* Latter I'll use config - this is incorrect */
    ospf_iface=mb_alloc(p->pool, sizeof(struct ospf_iface));
    ospf_iface->iface=new;
    add_tail(&c->iface_list, NODE ospf_iface);
    ospf_iface_default(ospf_iface);
    init_list(&(ospf_iface->sk_list));
    if(ospf_open_socket(p, ospf_iface)!=NULL)
    {
      add_tail(&(ospf_iface->sk_list),NODE ospf_iface);
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
  struct proto *p = proto_new(c, sizeof(struct proto));

  DBG(" OSPF: Init.\n");
  init_list(&((struct ospf_config *)c)->iface_list);
  p->neigh_notify = NULL;
  p->if_notify = NULL;
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

