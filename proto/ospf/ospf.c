/*
 *	BIRD -- OSPF
 *
 *	(c) 1999 - 2000 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

/**
 * DOC: Open Shortest Path First (OSPF)
 * 
 * The OSPF protocol is quite complicated and its complex implemenation is
 * split to many files. In |ospf.c|, you will find mainly the interface
 * for communication with the core (e.g., reconfiguration hooks, shutdown
 * and initialisation and so on). In |packet.c|, you will find various
 * functions for sending and receiving generic OSPF packets. There are
 * also routines for authentication and checksumming. File |iface.c| contains
 * the interface state machine and functions for allocation and deallocation of OSPF's
 * interface data structures. Source |neighbor.c| includes the neighbor state
 * machine and functions for election of Designated Router and Backup
 * Designated router. In |hello.c|, there are routines for sending
 * and receiving of hello packets as well as functions for maintaining
 * wait times and the inactivity timer. Files |lsreq.c|, |lsack.c|, |dbdes.c|
 * contain functions for sending and receiving of link-state requests,
 * link-state acknowledgements and database descriptions respectively.
 * In |lsupd.c|, there are functions for sending and receiving
 * of link-state updates and also the flooding algorithm. Source |topology.c| is
 * a place where routines for searching LSAs in the link-state database,
 * adding and deleting them reside, there also are functions for originating
 * of various types of LSAs (router LSA, net LSA, external LSA). File |rt.c|
 * contains routines for calculating the routing table. |lsalib.c| is a set
 * of various functions for working with the LSAs (endianity conversions,
 * calculation of checksum etc.).
 *
 * One instance of the protocol is able to hold LSA databases for
 * multiple OSPF areas, to exchange routing information between
 * multiple neighbors and to calculate the routing tables. The core
 * structure is &proto_ospf to which multiple &ospf_area and
 * &ospf_iface structures are connected. &ospf_area is also connected to
 * &top_hash_graph which is a dynamic hashing structure that
 * describes the link-state database. It allows fast search, addition
 * and deletion. Each LSA is kept in two pieces: header and body. Both of them are
 * kept in the endianity of the CPU.
 * 
 * Every area has its own area_disp() which is
 * responsible for late originating of router LSA, calculating
 * of the routing table and it also ages and flushes the LSAs. This
 * function is called in regular intervals.
 * To every &ospf_iface, we connect one or more
 * &ospf_neighbor's -- a structure containing many timers and queues
 * for building adjacency and for exchange of routing messages.
 *
 * BIRD's OSPF implementation respects RFC2328 in every detail, but
 * some of internal algorithms do differ. The RFC recommends making a snapshot
 * of the link-state database when a new adjacency is forming and sending
 * the database description packets based on the information in this 
 * snapshot. The database can be quite large in some networks, so
 * rather we walk through a &slist structure which allows us to
 * continue even if the actual LSA we were working with is deleted. New
 * LSAs are added at the tail of this &slist.
 *
 * We also don't keep a separate OSPF routing table, because the core
 * helps us by being able to recognize when a route is updated
 * to an identical one and it suppresses the update automatically.
 * Due to this, we can flush all the routes we've recalculated and
 * also those we've deleted to the core's routing table and the
 * core will take care of the rest. This simplifies the process
 * and conserves memory.
 */

#include "ospf.h"

static int
ospf_start(struct proto *p)
{
  struct proto_ospf *po=(struct proto_ospf *)p;
  struct ospf_config *c=(struct ospf_config *)(p->cf);
  struct ospf_area_config *ac;
  struct ospf_area *oa;
  struct area_net *anet,*antmp;

  fib_init(&po->efib,p->pool,sizeof(struct extfib),16,init_efib);
  init_list(&(po->iface_list));
  init_list(&(po->area_list));
  po->areano=0;
  if(EMPTY_LIST(c->area_list))
  {
    log("%s: Cannot start, no OSPF areas configured", p->name);
    return PS_DOWN;
  }

  WALK_LIST(ac,c->area_list)
  {
    oa=mb_allocz(po->proto.pool, sizeof(struct ospf_area));
    add_tail(&po->area_list, NODE oa);
    po->areano++;
    oa->stub=ac->stub;
    oa->tick=ac->tick;
    oa->areaid=ac->areaid;
    oa->gr=ospf_top_new(po);
    s_init_list(&(oa->lsal));
    oa->rt=NULL;
    oa->po=po;
    oa->disp_timer=tm_new(po->proto.pool);
    oa->disp_timer->data=oa;
    oa->disp_timer->randomize=0;
    oa->disp_timer->hook=area_disp;
    oa->disp_timer->recurrent=oa->tick;
    tm_start(oa->disp_timer,oa->tick);
    oa->calcrt=0;
    oa->origrt=0;
    init_list(&oa->net_list);
    WALK_LIST(anet,ac->net_list)
    {
      antmp=mb_allocz(po->proto.pool, sizeof(struct area_net));
      antmp->net=anet->net;
      antmp->mlen=anet->mlen;
      antmp->hidden=anet->hidden;
      add_tail(&oa->net_list, NODE antmp);
    }
    fib_init(&oa->infib,po->proto.pool,sizeof(struct infib),16,init_infib);
  }
  return PS_UP;
}

static void
ospf_dump(struct proto *p)
{
  char areastr[20];
  struct ospf_iface *ifa;
  struct ospf_neighbor *n;
  struct ospf_config *c = (void *) p->cf;
  struct proto_ospf *po=(struct proto_ospf *)p;
  struct ospf_area *oa;

  OSPF_TRACE(D_EVENTS, "Area number: %d", po->areano);

  WALK_LIST(ifa, po->iface_list)
  {
    OSPF_TRACE(D_EVENTS, "Interface: %s", ifa->iface->name);
    OSPF_TRACE(D_EVENTS, "state: %u", ifa->state);
    OSPF_TRACE(D_EVENTS, "DR:  %I", ifa->drid);
    OSPF_TRACE(D_EVENTS, "BDR: %I", ifa->bdrid);
    WALK_LIST(n, ifa->neigh_list)
    {
      OSPF_TRACE(D_EVENTS, "  neighbor %I in state %u", n->rid, n->state);
    }
  }

  WALK_LIST(NODE oa,po->area_list)
  {
    OSPF_TRACE(D_EVENTS, "LSA graph dump for area \"%I\" start:", oa->areaid);
    ospf_top_dump(oa->gr,p);
    OSPF_TRACE(D_EVENTS, "LSA graph dump for area \"%I\" finished", oa->areaid);
  }
  neigh_dump_all();
}

static struct proto *
ospf_init(struct proto_config *c)
{
  struct proto *p = proto_new(c, sizeof(struct proto_ospf));
  struct proto_ospf *po=(struct proto_ospf *)p;
  struct ospf_config *oc=(struct ospf_config *)c;
  struct ospf_area_config *ac;
  struct ospf_iface_patt *patt;

  p->import_control = ospf_import_control;
  p->make_tmp_attrs = ospf_make_tmp_attrs;
  p->store_tmp_attrs = ospf_store_tmp_attrs;
  p->rt_notify = ospf_rt_notify;
  p->if_notify = ospf_if_notify;
  p->rte_better = ospf_rte_better;
  p->rte_same = ospf_rte_same;

  po->rfc1583=oc->rfc1583;
  po->ebit=0;
  return p;
}

/* If new is better return 1 */
static int
ospf_rte_better(struct rte *new, struct rte *old)
{
  struct proto *p = new->attrs->proto;

  if(new->u.ospf.metric1==LSINFINITY) return 0;

  /* External paths are always longer that internal */
  if(((new->attrs->source==RTS_OSPF) || (new->attrs->source==RTS_OSPF_IA))
    && (old->attrs->source==RTS_OSPF_EXT)) return 1;
  if(((old->attrs->source==RTS_OSPF) || (old->attrs->source==RTS_OSPF_IA))
    && (new->attrs->source==RTS_OSPF_EXT)) return 0;

  if(new->u.ospf.metric2<old->u.ospf.metric2)
  {
    if(old->u.ospf.metric2==LSINFINITY) return 0; /* Old is E1, new is E2 */
    return 1;	/* Both are E2 */
  }
	
  if(new->u.ospf.metric2>old->u.ospf.metric2)
  {
    if(new->u.ospf.metric2==LSINFINITY) return 1; /* New is E1, old is E2 */
    return 0;	/* Both are E2 */
  }

  /* 
   * E2 metrics are the same. It means that:
   * 1) Paths are E2 with same metric
   * 2) Paths are E1.
   */

  if(new->u.ospf.metric1<old->u.ospf.metric1) return 1;
  return 0;	/* Old is shorter or same */
}

static int
ospf_rte_same(struct rte *new, struct rte *old)
{
  /* new->attrs == old->attrs always */
  return
    new->u.ospf.metric1 == old->u.ospf.metric1 &&
    new->u.ospf.metric2 == old->u.ospf.metric2 &&
    new->u.ospf.tag     == old->u.ospf.tag;
}

static ea_list *
ospf_build_attrs(ea_list *next, struct linpool *pool, u32 m1, u32 m2, u32 tag)
{
  struct ea_list *l = lp_alloc(pool, sizeof(struct ea_list) + 3*sizeof(eattr));

  l->next = next;
  l->flags = EALF_SORTED;
  l->count = 3;
  l->attrs[0].id = EA_OSPF_METRIC1;
  l->attrs[0].flags = 0;
  l->attrs[0].type = EAF_TYPE_INT | EAF_TEMP;
  l->attrs[0].u.data = m1;
  l->attrs[1].id = EA_OSPF_METRIC2;
  l->attrs[1].flags = 0;
  l->attrs[1].type = EAF_TYPE_INT | EAF_TEMP;
  l->attrs[1].u.data = m2;
  l->attrs[2].id = EA_OSPF_TAG;
  l->attrs[2].flags = 0;
  l->attrs[2].type = EAF_TYPE_INT | EAF_TEMP;
  l->attrs[2].u.data = tag;
  return l;
}

void
schedule_rt_lsa(struct ospf_area *oa)
{
  struct proto_ospf *po=oa->po;
  struct proto *p=&po->proto;

  OSPF_TRACE(D_EVENTS, "Scheduling RT lsa origination for area %I.",
    oa->areaid);
  oa->origrt=1;
}

void
schedule_rtcalc(struct ospf_area *oa)
{
  struct proto_ospf *po=oa->po;
  struct proto *p=&po->proto;

  OSPF_TRACE(D_EVENTS, "Scheduling RT calculation for area %I.", 
    oa->areaid);
  oa->calcrt=1;
}

/**
 * area_disp - invokes link-state database aging, origination of
 * router LSA and routing table calculation
 * @timer: it's called every @ospf_area->tick seconds
 *
 * It invokes aging and when @ospf_area->origrt is set to 1, start
 * function for origination of router LSA and network LSAs.
 * It also starts routing
 * table calculation when @ospf_area->calcrt is set.
 */
void
area_disp(timer *timer)
{
  struct ospf_area *oa=timer->data;
  struct top_hash_entry *en,*nxt;
  struct proto_ospf *po=oa->po;
  struct ospf_iface *ifa;

  /* Now try to originage rt_lsa */
  if(oa->origrt) originate_rt_lsa(oa);

  /* Now try to originate network LSA's */
  WALK_LIST(ifa, po->iface_list)
  {
    if(ifa->orignet&&(ifa->an==oa->areaid)) originate_net_lsa(ifa);
  }

  /* Age LSA DB */
  ospf_age(oa);

  /* Calculate routing table */
  if(oa->calcrt) ospf_rt_spfa(oa);
  oa->calcrt=0;
}

/**
 * ospf_import_control - accept or reject new route from nest's routing table
 * @p: current instance of protocol
 * @new: the new route
 * @attrs: list of attributes
 * @pool: pool for allocation of attributes
 *
 * Its quite simple. It does not accept our own routes and leaves the decision on
 * import to the filters.
 */

int
ospf_import_control(struct proto *p, rte **new, ea_list **attrs, struct linpool *pool)
{
  rte *e=*new;
  struct proto_ospf *po=(struct proto_ospf *)p;

  if(p==e->attrs->proto) return -1;	/* Reject our own routes */
  *attrs = ospf_build_attrs(*attrs, pool, LSINFINITY, 10000, 0);
  return 0;				/* Leave decision to the filters */
}

struct ea_list *
ospf_make_tmp_attrs(struct rte *rt, struct linpool *pool)
{
  return ospf_build_attrs(NULL, pool, rt->u.ospf.metric1, rt->u.ospf.metric2, rt->u.ospf.tag);
}

void
ospf_store_tmp_attrs(struct rte *rt, struct ea_list *attrs)
{
  rt->u.ospf.metric1 = ea_get_int(attrs, EA_OSPF_METRIC1, LSINFINITY);
  rt->u.ospf.metric2 = ea_get_int(attrs, EA_OSPF_METRIC2, 10000);
  rt->u.ospf.tag     = ea_get_int(attrs, EA_OSPF_TAG,     0);
}

/**
 * ospf_shutdown - Finish of OSPF instance
 * @p: current instance of protocol
 *
 * RFC does not define any action that should be taken before router
 * shutdown. To make my neighbors react as fast as possible, I send
 * them hello packet with empty neighbor list. They should start
 * their neighbor state machine with event %NEIGHBOR_1WAY.
 */

static int
ospf_shutdown(struct proto *p)
{
  struct proto_ospf *po=(struct proto_ospf *)p;
  struct ospf_iface *ifa;
  struct ospf_neighbor *n;
  struct ospf_area *oa;
  OSPF_TRACE(D_EVENTS, "Shutdown requested");

  /* And send to all my neighbors 1WAY */
  WALK_LIST(ifa, po->iface_list)
  {
    init_list(&ifa->neigh_list);
    hello_timer_hook(ifa->hello_timer);
  }
  
  return PS_DOWN;
}

void
ospf_rt_notify(struct proto *p, net *n, rte *new, rte *old, ea_list *attrs)
{
  struct proto_ospf *po=(struct proto_ospf *)p;

/* Temporarily down write anything
  OSPF_TRACE(D_EVENTS, "Got route %I/%d %s", p->name, n->n.prefix,
    n->n.pxlen, new ? "up" : "down");
*/

  if(new)		/* Got some new route */
  {
    originate_ext_lsa(n, new, po, attrs);
  }
  else
  {
    u32 rtid=po->proto.cf->global->router_id;
    struct ospf_area *oa;
    struct top_hash_entry *en;
    u32 pr=ipa_to_u32(n->n.prefix);
    struct ospf_lsa_ext *ext;
    int i;

    /* Flush old external LSA */
    WALK_LIST(oa, po->area_list)
    {
      for(i=0;i<MAXNETS;i++,pr++)
      {
        if(en=ospf_hash_find(oa->gr, pr, rtid, LSA_T_EXT))
        {
          ext=en->lsa_body;
          if(ipa_compare(ext->netmask, ipa_mkmask(n->n.pxlen))==0)
          {
            net_flush_lsa(en,po,oa);
            break;
          }
        }
      }
    }
  }
}

static void
ospf_get_status(struct proto *p, byte *buf)
{
  struct proto_ospf *po=(struct proto_ospf *)p;

  if (p->proto_state == PS_DOWN) buf[0] = 0;
  else
  {
    struct ospf_iface *ifa;
    struct ospf_neighbor *n;
    int adj=0;

    WALK_LIST(ifa,po->iface_list)
      WALK_LIST(n,ifa->neigh_list)
        if(n->state==NEIGHBOR_FULL) adj=1;

    if(adj==0) strcpy(buf, "Alone");
    else strcpy(buf, "Running");
  }
}

static void
ospf_get_route_info(rte *rte, byte *buf, ea_list *attrs)
{
  char met=' ';
  char type=' ';

  if(rte->attrs->source==RTS_OSPF_EXT)
  {
    met='1';
    type='E';

  }
  if(rte->u.ospf.metric2!=LSINFINITY) met='2';
  if(rte->attrs->source==RTS_OSPF_IA) type='A';
  if(rte->attrs->source==RTS_OSPF) type='I';
  buf += bsprintf(buf, " %c", type);
  if(met!=' ') buf += bsprintf(buf, "%c", met);
  buf += bsprintf(buf, " (%d/%d", rte->pref, rte->u.ospf.metric1);
  if(rte->u.ospf.metric2!=LSINFINITY) buf += bsprintf(buf, "/%d", rte->u.ospf.metric2);
  buf += bsprintf(buf, ")");
  if(rte->attrs->source==RTS_OSPF_EXT && rte->u.ospf.tag)
  {
    buf += bsprintf(buf, " [%x]", rte->u.ospf.tag);
  }
}

static int
ospf_get_attr(eattr *a, byte *buf)
{
  switch (a->id)
    {
    case EA_OSPF_METRIC1: bsprintf(buf, "metric1"); return GA_NAME;
    case EA_OSPF_METRIC2: bsprintf(buf, "metric2"); return GA_NAME;
    case EA_OSPF_TAG: bsprintf(buf, "tag: %08x", a->u.data); return GA_FULL;
    default: return GA_UNKNOWN;
    }
}

static int
ospf_patt_compare(struct ospf_iface_patt *a, struct ospf_iface_patt *b)
{
  return ((a->type==b->type)&&(a->priority==b->priority));
}

/**
 * ospf_reconfigure - reconfiguration hook
 * @p: current instance of protocol (with old configuration)
 * @c: new configuration requested by user
 *
 * This hook tries to be a little bit intelligent. Instance of OSPF
 * will survive change of many constants like hello interval,
 * password change, addition or deletion of some neighbor on
 * nonbroadcast network, cost of interface, etc.
 */
static int
ospf_reconfigure(struct proto *p, struct proto_config *c)
{
  struct ospf_config *old=(struct ospf_config *)(p->cf);
  struct ospf_config *new=(struct ospf_config *)c;
  struct ospf_area_config *ac1,*ac2;
  struct proto_ospf *po=( struct proto_ospf *)p;
  struct ospf_iface_patt *ip1,*ip2;
  struct ospf_iface *ifa;
  struct nbma_node *nb1,*nb2,*nbnx;
  struct ospf_area *oa=NULL;
  struct area_net *anet,*antmp;
  int found;

  po->rfc1583=new->rfc1583;
  WALK_LIST(oa, po->area_list)	/* Routing table must be recalculated */
  {
    schedule_rtcalc(oa);
  }

  ac1=HEAD(old->area_list);
  ac2=HEAD(new->area_list);

  /* I should get it in same order */
  
  while(((NODE (ac1))->next!=NULL) && ((NODE (ac2))->next!=NULL))
  {
    if(ac1->areaid!=ac2->areaid) return 0;
    if(ac1->stub!=ac2->stub) return 0;	/* FIXME: non zero values can change */

    WALK_LIST(oa,po->area_list)
      if(oa->areaid==ac2->areaid) break;

    if(!oa) return 0;

    if(ac1->tick!=ac2->tick)
    {
      if(oa->areaid==ac2->areaid)
      {
        oa->tick=ac2->tick;
        tm_start(oa->disp_timer,oa->tick);
        OSPF_TRACE(D_EVENTS,
         "Changing tick interval on area %I from %d to %d",
         oa->areaid, ac1->tick, ac2->tick);
        break;
      }
    }

    /* Change net_list */
    WALK_LIST_DELSAFE(anet, antmp, oa->net_list)
    {
       rem_node(NODE anet);
       mb_free(anet);
    }
    WALK_LIST(anet, ac2->net_list)
    {
       antmp=mb_alloc(p->pool, sizeof(struct area_net));
       antmp->net=anet->net;
       antmp->mlen=anet->mlen;
       antmp->hidden=anet->hidden;
       add_tail(&oa->net_list, NODE antmp);
    }

    if(!iface_patts_equal(&ac1->patt_list, &ac2->patt_list,
      (void *) ospf_patt_compare))
        return 0;

    WALK_LIST(ifa, po->iface_list)
    {
      if(ip1=(struct ospf_iface_patt *)
        iface_patt_match(&ac1->patt_list, ifa->iface))
      {
        /* Now reconfigure interface */
	if(!(ip2=(struct ospf_iface_patt *)
	  iface_patt_match(&ac2->patt_list, ifa->iface))) return 0;

	/* HELLO TIMER */
	if(ip1->helloint!=ip2->helloint)
	{
	  ifa->helloint=ip2->helloint;
	  ifa->hello_timer->recurrent=ifa->helloint;
	  tm_start(ifa->hello_timer,ifa->helloint);
	  OSPF_TRACE(D_EVENTS,
	    "Changing hello interval on interface %s from %d to %d",
	    ifa->iface->name,ip1->helloint,ip2->helloint);
	}

	/* POLL TIMER */
	if(ip1->pollint!=ip2->pollint)
	{
	  ifa->pollint=ip2->helloint;
	  ifa->poll_timer->recurrent=ifa->pollint;
	  tm_start(ifa->poll_timer,ifa->pollint);
	  OSPF_TRACE(D_EVENTS,
	    "Changing poll interval on interface %s from %d to %d",
	    ifa->iface->name,ip1->pollint,ip2->pollint);
	}

	/* COST */
	if(ip1->cost!=ip2->cost)
	{
	  ifa->cost=ip2->cost;
	  OSPF_TRACE(D_EVENTS,
	    "Changing cost interface %s from %d to %d",
	    ifa->iface->name,ip1->cost,ip2->cost);
	  schedule_rt_lsa(ifa->oa);
	}

	/* strict nbma */
	if((ip1->strictnbma==0)&&(ip2->strictnbma!=0))
	{
	  ifa->strictnbma=ip2->strictnbma;
	  OSPF_TRACE(D_EVENTS,
	    "Interface %s is now strict NBMA.",
	    ifa->iface->name);
	}
	if((ip1->strictnbma!=0)&&(ip2->strictnbma==0))
	{
	  ifa->strictnbma=ip2->strictnbma;
	  OSPF_TRACE(D_EVENTS,
	    "Interface %s is no longer strict NBMA.",
	    ifa->iface->name);
	}

	/* stub */
	if((ip1->stub==0)&&(ip2->stub!=0))
	{
	  ifa->stub=ip2->stub;
	  OSPF_TRACE(D_EVENTS,
	    "Interface %s is now stub.",
	    ifa->iface->name);
	}
	if((ip1->stub!=0)&&(ip2->stub==0)&&
	  ((ifa->ioprob & OSPF_I_IP)==0)&&
	  (((ifa->ioprob & OSPF_I_MC)==0)||
	  (ifa->type==OSPF_IT_NBMA)))
	{
	  ifa->stub=ip2->stub;
	  OSPF_TRACE(D_EVENTS,
	    "Interface %s is no longer stub.",
	    ifa->iface->name);
	}

	/* AUTHENTICATION */
	if(ip1->autype!=ip2->autype)
	{
	  ifa->autype=ip2->autype;
	  OSPF_TRACE(D_EVENTS,
	    "Changing authentication type on interface %s",
	    ifa->iface->name);
	}
	if(strncmp(ip1->password,ip2->password,8)!=0)
	{
	  memcpy(ifa->aukey,ip2->password,8);
	  OSPF_TRACE(D_EVENTS,
	    "Changing password on interface %s",
	    ifa->iface->name);
	}

	/* RXMT */
	if(ip1->rxmtint!=ip2->rxmtint)
	{
	  ifa->rxmtint=ip2->rxmtint;
	  OSPF_TRACE(D_EVENTS,
	    "Changing retransmit interval on interface %s from %d to %d",
	    ifa->iface->name,ip1->rxmtint,ip2->rxmtint);
	}

	/* WAIT */
	if(ip1->waitint!=ip2->waitint)
	{
	  ifa->waitint=ip2->waitint;
	  if(ifa->wait_timer->expires!=0)
	    tm_start(ifa->wait_timer,ifa->waitint);
	  OSPF_TRACE(D_EVENTS,
	    "Changing wait interval on interface %s from %d to %d",
	    ifa->iface->name,ip1->waitint,ip2->waitint);
	}

	/* INFTRANS */
	if(ip1->inftransdelay!=ip2->inftransdelay)
	{
	  ifa->inftransdelay=ip2->inftransdelay;
	  OSPF_TRACE(D_EVENTS,
	    "Changing transmit delay on interface %s from %d to %d",
	    ifa->iface->name,ip1->inftransdelay,ip2->inftransdelay);
	}

	/* DEAD COUNT */
	if(ip1->deadc!=ip2->deadc)
	{
	  ifa->deadc=ip2->deadc;
	  OSPF_TRACE(D_EVENTS,
	    "Changing dead count on interface %s from %d to %d",
	    ifa->iface->name,ip1->deadc,ip2->deadc);
	}

	/* NBMA LIST */
	/* First remove old */
	WALK_LIST_DELSAFE(nb1, nbnx, ifa->nbma_list)
	{
	  found=0;
	  WALK_LIST(nb2, ip2->nbma_list)
	    if(ipa_compare(nb1->ip,nb2->ip)==0)
	    {
	      found=1;
	      if(nb1->eligible!=nb2->eligible)
	        OSPF_TRACE(D_EVENTS,
	          "Changing neighbor eligibility %I on interface %s",
	          nb1->ip,ifa->iface->name);
	      break;
	    }

	  if(!found)
	  {
	    OSPF_TRACE(D_EVENTS,
	      "Removing NBMA neighbor %I on interface %s",
	      nb1->ip,ifa->iface->name);
	    rem_node(NODE nb1);
	    mb_free(nb1);
	  }
        }
	/* And then add new */
	WALK_LIST(nb2, ip2->nbma_list)
	{
	  found=0;
	  WALK_LIST(nb1, ifa->nbma_list)
	    if(ipa_compare(nb1->ip,nb2->ip)==0)
	    {
	      found=1;
	      break;
	    }
	  if(!found)
	  {
	    nb1=mb_alloc(p->pool,sizeof(struct nbma_node));
	    nb1->ip=nb2->ip;
	    nb1->eligible=nb2->eligible;
	    add_tail(&ifa->nbma_list, NODE nb1);
	    OSPF_TRACE(D_EVENTS,
	      "Adding NBMA neighbor %I on interface %s",
	      nb1->ip,ifa->iface->name);
	  }
        }
      }
    }

    NODE ac1=(NODE (ac1))->next;
    NODE ac2=(NODE (ac2))->next;
  }

  if(((NODE (ac1))->next)!=((NODE (ac2))->next))
    return 0;	/* One is not null */

  return 1;	/* Everything OK :-) */
}

void
ospf_sh_neigh(struct proto *p, char *iff)
{
  struct ospf_iface *ifa=NULL,*f;
  struct ospf_neighbor *n;
  struct proto_ospf *po=(struct proto_ospf *)p;

  if(p->proto_state != PS_UP)
  {
    cli_msg(-1013,"%s: is not up", p->name);
    cli_msg(0,"");
    return;
  }
  
  if(iff!=NULL)
  {
    WALK_LIST(f, po->iface_list)
    {
      if(strcmp(iff,f->iface->name)==0)
      {
        ifa=f;
        break;
      }
    }
    if(ifa==NULL)
    {
      cli_msg(0,"");
      return;
    }
    cli_msg(-1013,"%s:", p->name);
    cli_msg(-1013,"%-12s\t%3s\t%-15s\t%-5s\t%-12s\t%-10s","Router ID","Pri",
      "     State", "DTime", "Router IP", "Interface");
    WALK_LIST(n, ifa->neigh_list) ospf_sh_neigh_info(n);
    cli_msg(0,"");
    return;
  }

  cli_msg(-1013,"%s:", p->name);
  cli_msg(-1013,"%-12s\t%3s\t%-15s\t%-5s\t%-12s\t%-10s","Router ID","Pri",
    "     State", "DTime", "Router IP", "Interface");
  WALK_LIST(ifa,po->iface_list)
    WALK_LIST(n, ifa->neigh_list)
      ospf_sh_neigh_info(n);
  cli_msg(0,"");
}

void
ospf_sh(struct proto *p)
{
  struct ospf_area *oa;
  struct proto_ospf *po=(struct proto_ospf *)p;
  struct ospf_iface *ifa;
  struct ospf_neighbor *n;
  int ifano;
  int nno;
  int adjno;

  if(p->proto_state != PS_UP)
  {
    cli_msg(-1014,"%s: is not up", p->name);
    cli_msg(0,"");
    return;
  }

  cli_msg(-1014,"%s:", p->name);
  cli_msg(-1014,"Number of areas: %u", po->areano);
  
  WALK_LIST(oa,po->area_list)
  {
    cli_msg(-1014,"\tArea: %I (%u) %s", oa->areaid, oa->areaid,
      oa->areaid==0 ? "[BACKBONE]" : "");
    ifano=0;
    nno=0;
    adjno=0;
    WALK_LIST(ifa, po->iface_list)
    {
      if(oa==ifa->oa) ifano++;
      WALK_LIST(n, ifa->neigh_list)
      {
        nno++;
        if(n->state==NEIGHBOR_FULL) adjno++;
      }
    }
    cli_msg(-1014,"\t\tStub:\t%s", oa->stub ? "Yes" : "No");
    cli_msg(-1014,"\t\tRT scheduler tick:\t%u", oa->tick);
    cli_msg(-1014,"\t\tNumber of interfaces:\t%u", ifano);
    cli_msg(-1014,"\t\tNumber of LSAs in DB:\t%u", oa->gr->hash_entries);
    cli_msg(-1014,"\t\tNumber of neighbors:\t%u", nno);
    cli_msg(-1014,"\t\tNumber of adjacent neighbors:\t%u", adjno);
    if(!EMPTY_LIST(oa->net_list))
    {
      struct area_net *anet;
      cli_msg(-1014,"\t\tArea networks:");
      WALK_LIST(anet, oa->net_list)
      {
        cli_msg(-1014,"\t\t\t%1I/%u\t%s", anet->net, anet->mlen,
          anet->hidden ? "Hidden" : "Advertise");
      }
    }
  }
  cli_msg(0,"");
}

void
ospf_sh_iface(struct proto *p, char *iff)
{
  struct ospf_area *oa;
  struct proto_ospf *po=(struct proto_ospf *)p;
  struct ospf_iface *ifa=NULL,*f;
  struct ospf_neighbor *n;
  int ifano;
  int nno;
  int adjno;

  if(p->proto_state != PS_UP)
  {
    cli_msg(-1015,"%s: is not up", p->name);
    cli_msg(0,"");
    return;
  }

  if(iff!=NULL)
  {
    WALK_LIST(f, po->iface_list)
    {
      if(strcmp(iff,f->iface->name)==0)
      {
        ifa=f;
        break;
      }
    }

    if(ifa==NULL)
    {
      cli_msg(0,"");
      return;
    }
    cli_msg(-1015,"%s:", p->name);
    ospf_iface_info(ifa);
    cli_msg(0,"");
    return;
  }
  cli_msg(-1015,"%s:", p->name);
  WALK_LIST(ifa, po->iface_list) ospf_iface_info(ifa);
  cli_msg(0,"");
}

struct protocol proto_ospf = {
  name: 		"OSPF",
  template:		"ospf%d",
  attr_class:		EAP_OSPF,
  init:			ospf_init,
  dump:			ospf_dump,
  start:		ospf_start,
  shutdown:		ospf_shutdown,
  get_route_info:	ospf_get_route_info,
  get_attr:		ospf_get_attr,
  get_status:		ospf_get_status,
  reconfigure:		ospf_reconfigure
};

