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

  /* Create graph of LSA's */
  po->areano=1;		/* FIXME should respect config! */
  po->firstarea=(struct ospf_area *)cfg_alloc(sizeof(struct ospf_area));
  po->firstarea->gr=ospf_top_new(po);
  po->firstarea->next=NULL;
  po->firstarea->areaid=0;

  po->areano=0;		/* Waiting for interfaces comming up */
  po->firstarea=NULL;
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

  debug("%s: AreaID: %u\n", p->name, c->area );

  WALK_LIST(ifa, po->iface_list)
  {
    debug("%s: Interface: %s\n", p->name, ifa->iface->name);
    debug("%s:  state: %u\n", p->name, ifa->state);
    debug("%s:  DR:  %u\n", p->name, ifa->drid);
    debug("%s:  BDR: %u\n", p->name, ifa->bdrid);
    WALK_LIST(n, ifa->neigh_list)
    {
      debug("%s:   neighbor %u in state %u\n", p->name, n->rid, n->state);
    }
  }

  oa=po->firstarea;
  while(oa!=NULL)
  {
    debug("\n%s: LSA graph dump for area \"%d\" start:\n", p->name,oa->areaid);
    ospf_top_dump(oa->gr);
    debug("%s: LSA graph dump for area \"%d\" finished\n\n", p->name,
      oa->areaid);
    oa=oa->next;
  }

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
  template:	"ospf%d",
  init:		ospf_init,
  dump:		ospf_dump,
  start:	ospf_start,
  preconfig:	ospf_preconfig,
  postconfig:	ospf_postconfig,
};

