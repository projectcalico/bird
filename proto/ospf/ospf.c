/*
 *	BIRD -- OSPF
 *
 *	(c) 1999 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

static int
ospf_start(struct proto *p)
{
  struct proto_ospf *po=(struct proto_ospf *)p;
  DBG("%s: Start\n",p->name);

  p->if_notify=ospf_if_notify;
  p->rte_better=ospf_rte_better;
  p->rte_same=ospf_rte_same;
  fib_init(&po->efib,p->pool,sizeof(struct extfib),16,init_efib);

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

  debug("%s: Area number: %d\n", p->name, po->areano);

  WALK_LIST(ifa, po->iface_list)
  {
    debug("%s: Interface: %s\n", p->name, ifa->iface->name);
    debug("%s:  state: %u\n", p->name, ifa->state);
    debug("%s:  DR:  %I\n", p->name, ifa->drid);
    debug("%s:  BDR: %I\n", p->name, ifa->bdrid);
    WALK_LIST(n, ifa->neigh_list)
    {
      debug("%s:   neighbor %I in state %u\n", p->name, n->rid, n->state);
    }
  }

  WALK_LIST(NODE oa,po->area_list)
  {
    debug("\n%s: LSA graph dump for area \"%I\" start:\n", p->name,oa->areaid);
    ospf_top_dump(oa->gr);
    debug("%s: LSA graph dump for area \"%I\" finished\n\n", p->name,
      oa->areaid);
  }
  neigh_dump_all();
}

static struct proto *
ospf_init(struct proto_config *c)
{
  struct proto *p = proto_new(c, sizeof(struct proto_ospf));
  struct proto_ospf *po=(struct proto_ospf *)p;

  debug("OSPF: Init requested.\n");
  p->neigh_notify = NULL;
  p->if_notify = NULL;
  init_list(&(po->iface_list));
  init_list(&(po->area_list));
  p->import_control = ospf_import_control;
  p->rt_notify = ospf_rt_notify;

  return p;
}

/* If new is better return 1 */
static int
ospf_rte_better(struct rte *new, struct rte *old)
{
  struct proto *p = new->attrs->proto;

  if(new->u.ospf.metric1=LSINFINITY) return 0;

  if(((new->attrs->source==RTS_OSPF) || (new->attrs->source==RTS_OSPF_IA))
    && (old->attrs->source==RTS_OSPF_EXT)) return 1;

  if(((old->attrs->source==RTS_OSPF) || (old->attrs->source==RTS_OSPF_IA))
    && (new->attrs->source==RTS_OSPF_EXT)) return 0;

  if(new->u.ospf.metric2!=0)
  {
    if(old->u.ospf.metric2==0) return 0;
    if(new->u.ospf.metric2<old->u.ospf.metric2) return 1;
    return 0;
  }
  else
  {
    if(old->u.ospf.metric2!=0) return 1;
    if(new->u.ospf.metric1<old->u.ospf.metric1) return 1;
    return 0;
  }
}

static int
ospf_rte_same(struct rte *new, struct rte *old)
{
  struct proto *p = new->attrs->proto;

  if(new->attrs->source!=old->attrs->source) return 0;
  if(new->u.ospf.metric1!=old->u.ospf.metric1) return 0;
  if(new->u.ospf.metric2!=old->u.ospf.metric2) return 0;
  return 1;
}

int
ospf_import_control(struct proto *p, rte **new, ea_list **attrs, struct linpool *pool)
{
  rte *e=*new;
  struct proto_ospf *po=(struct proto_ospf *)p;

  if(p==e->attrs->proto) return -1;

  return 0;
}

static int
ospf_shutdown(struct proto *p)
{
  struct proto_ospf *po=(struct proto_ospf *)p;
  struct ospf_iface *ifa;
  struct ospf_neighbor *n;
  struct ospf_area *oa;
  debug("%s: Shutdown requested\n", p->name);
  /* first of all flush my rt LSA */

  WALK_LIST(oa,po->area_list)
  {
    net_flush_lsa(oa->rt,po,oa);
  }


  /* And send to all my neighbors 1WAY */
  WALK_LIST(ifa, po->iface_list)
  {
    WALK_LIST(n, ifa->neigh_list) neigh_chstate(n,NEIGHBOR_DOWN);
    hello_timer_hook(ifa->hello_timer);
  }
  
  return PS_DOWN;
}

void
ospf_rt_notify(struct proto *p, net *n, rte *new, rte *old, ea_list *attrs)
{
  struct proto_ospf *po=(struct proto_ospf *)p;

  debug("%s: Got route %I/%d %s\n", p->name, n->n.prefix,
    n->n.pxlen, new ? "up" : "down");

  if(new)		/* Got some new route */
  {
    originate_ext_lsa(n, old, po);
  }
  else
  {
    u32 rtid=po->proto.cf->global->router_id;
    struct ospf_area *oa;
    struct top_hash_entry *en;

    /* Flush old external LSA */
    WALK_LIST(oa, po->area_list)
    {
      if(en=ospf_hash_find(oa->gr, ipa_to_u32(n->n.prefix), rtid, LSA_T_EXT))
        net_flush_lsa(en,po,oa);
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

  if(rte->u.ospf.type=='E') met='1';
  if(rte->u.ospf.metric2!=0) met='2';
  buf += bsprintf(buf, " %c%c %d", rte->u.ospf.type, met, 
    (rte->u.ospf.metric2==0) ? rte->u.ospf.metric1 : rte->u.ospf.metric2);
}

struct protocol proto_ospf = {
  name: 		"OSPF",
  template:		"ospf%d",
  init:			ospf_init,
  dump:			ospf_dump,
  start:		ospf_start,
  shutdown:		ospf_shutdown,
  get_route_info:	ospf_get_route_info,
  get_status:		ospf_get_status
};
