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

  DBG(" OSPF: Init.\n");
  p->neigh_notify = NULL;
  p->if_notify = NULL;
  init_list(&(po->iface_list));
  init_list(&(po->area_list));

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

static void
ospf_postconfig(struct proto_config *c)
{
  DBG( " OSPF: postconfig\n" );
}

struct protocol proto_ospf = {
  name:		"OSPF",
  template:	"ospf%d",
  init:		ospf_init,
  dump:		ospf_dump,
  start:	ospf_start,
  postconfig:	ospf_postconfig,
};
