/*
 *	BIRD -- OSPF Topological Database
 *
 *	(c) 1999        Martin Mares <mj@ucw.cz>
 *	(c) 1999 - 2000 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include "nest/bird.h"
#include "lib/string.h"

#include "ospf.h"

#define HASH_DEF_ORDER 6		/* FIXME: Increase */
#define HASH_HI_MARK *4
#define HASH_HI_STEP 2
#define HASH_HI_MAX 16
#define HASH_LO_MARK /5
#define HASH_LO_STEP 2
#define HASH_LO_MIN 8

unsigned int
make_rt_lsa(struct ospf_area *oa, struct proto_ospf *p)
{
  struct ospf_iface *ifa;
  int j=0,k=0,v=0,e=0,b=0;
  u16 i=0;
  struct ospf_lsa_rt *rt;
  struct ospf_lsa_rt_link *ln;
  struct ospf_neighbor *neigh;
  struct top_hash_entry *old;

  old=oa->rt;

  WALK_LIST (ifa, p->iface_list) i++;
  {
    if((ifa->an==oa->areaid) && (ifa->state!=OSPF_IS_DOWN))
    {
      i++;
      if(ifa->type==OSPF_IT_VLINK) v=1;
    }
  }
  rt=mb_allocz(p->proto.pool, sizeof(struct ospf_lsa_rt)+
    i*sizeof(struct ospf_lsa_rt_link));
  if((p->areano>1) && (!oa->stub)) e=1;
  rt->VEB=(v>>LSA_RT_V)+(e>>LSA_RT_E)+(b>>LSA_RT_B);
  ln=(struct ospf_lsa_rt_link *)(rt+1);

  WALK_LIST (ifa, p->iface_list)
  {
    if((ifa->an==oa->areaid) && (ifa->state!=OSPF_IS_DOWN))
    {
      if(ifa->state==OSPF_IS_LOOP)
      {
        ln->type=3;
	ln->id=ipa_to_u32(ifa->iface->addr->ip);
	ln->data=0xffffffff;
	ln->metric=0;
	ln->notos=0;
      }
      else
      {
        switch(ifa->type)
	{
          case OSPF_IT_PTP:		/* rfc2328 - pg126 */
            neigh=(struct ospf_neighbor *)HEAD(ifa->neigh_list);
	    if((neigh!=NULL) || (neigh->state==NEIGHBOR_FULL))
	    {
               ln->type=LSART_PTP;
               ln->id=neigh->rid;
               ln->metric=ifa->cost;
               ln->notos=0;
               if(ifa->iface->flags && IA_UNNUMBERED)
               {
                 ln->data=ifa->iface->index;
               }
               else
               {
                 ln->id=ipa_to_u32(ifa->iface->addr->ip);
               }
	    }
	    else
	    {
	      if(ifa->state==OSPF_IS_PTP)
              {
		ln->type=LSART_STUB;
		ln->id=ln->id=ipa_to_u32(ifa->iface->addr->opposite);
		ln->metric=ifa->cost;
		ln->notos=0;
		ln->data=0xffffffff;
              }
              else
              {
		i--; /* No link added */
              }
	    }
            break;
	  case OSPF_IT_BCAST: /*FIXME Go on */
	  case OSPF_IT_NBMA:
            if(ifa->state==OSPF_IS_WAITING)
            {
              ln->type=LSART_STUB;
	      ln->id=ipa_to_u32(ifa->iface->addr->prefix);
	      ln->data=ipa_to_u32(ipa_mkmask(ifa->iface->addr->pxlen));
              ln->metric=ifa->cost;
              ln->notos=0;
            }
	    else
            {
              j=0,k=0;
              WALK_LIST(neigh, ifa->neigh_list)
	      {
	        if((neigh->rid==ifa->drid) &&
	          (neigh->state==NEIGHBOR_FULL)) k=1;
		if(neigh->state==NEIGHBOR_FULL) j=1;
	      }
              if(((ifa->state=OSPF_IS_DR) && (j==1)) || (k==1))
	      {
	        ln->type=LSART_NET;
		ln->id=ipa_to_u32(ifa->drip);
		ln->data=ipa_to_u32(ifa->iface->addr->ip);
		ln->metric=ifa->cost;
		ln->notos=0;
	      }
	      else
	      {
                ln->type=LSART_STUB;
  	        ln->id=ipa_to_u32(ifa->iface->addr->prefix);
	        ln->data=ipa_to_u32(ipa_mkmask(ifa->iface->addr->pxlen));
                ln->metric=ifa->cost;
                ln->notos=0;
	      }
            }
	    break;
	  case OSPF_IT_VLINK:	/* FIXME Add virtual links! */
	    i--;
	    break;
	}
      }
      if(ifa->type==OSPF_IT_VLINK) v=1;
    }
    ln=(ln+1);
  }
  rt->links=i;
  if(old->lsa_body!=NULL) mb_free(old->lsa_body);
  old->lsa_body=rt;
  return i*sizeof(struct ospf_lsa_rt_link)+sizeof(struct ospf_lsa_rt);
}
	

void
addifa_rtlsa(struct ospf_iface *ifa)
{
  struct ospf_area *oa;
  struct proto_ospf *po;
  u32 rtid;
  struct top_graph_rtlsa_link *li, *lih;

  po=ifa->proto;
  rtid=po->proto.cf->global->router_id;
  DBG("%s: New OSPF area \"%d\" adding.\n", po->proto.name, ifa->an);
  oa=NULL;


  WALK_LIST(NODE oa,po->area_list)
  {
    if(oa->areaid==ifa->an) break;
  }

  if(EMPTY_LIST(po->area_list) || (oa->areaid!=ifa->an))	/* New area */
  {
    struct ospf_lsa_header *lsa;

    oa=mb_alloc(po->proto.pool, sizeof(struct ospf_area));
    add_tail(&po->area_list,NODE oa);
    oa->areaid=ifa->an;
    oa->gr=ospf_top_new(po);
    s_init_list(&(oa->lsal));
    oa->rt=ospf_hash_get(oa->gr, rtid, rtid, LSA_T_RT);
    s_add_head(&(oa->lsal), (snode *)oa->rt);
    ((snode *)oa->rt)->next=NULL;
    lsa=&(oa->rt->lsa);
    oa->rt->lsa_body=NULL;
    lsa->age=0;
    lsa->sn=LSA_INITSEQNO;	/* FIXME Check it latter */
    po->areano++;
    DBG("%s: New OSPF area \"%d\" added.\n", po->proto.name, ifa->an);
  }

  ifa->oa=oa;
 
  oa->rt->lsa.length=make_rt_lsa(oa, po)+sizeof(struct ospf_lsa_header);
  oa->rt->lsa.checksum=0;
  lsasum_calculate(&(oa->rt->lsa),oa->rt->lsa_body,po);
  /*FIXME lsa_flood(oa->rt) */
}

  

static void
ospf_top_ht_alloc(struct top_graph *f)
{
  f->hash_size = 1 << f->hash_order;
  f->hash_mask = f->hash_size - 1;
  if (f->hash_order > HASH_HI_MAX - HASH_HI_STEP)
    f->hash_entries_max = ~0;
  else
    f->hash_entries_max = f->hash_size HASH_HI_MARK;
  if (f->hash_order < HASH_LO_MIN + HASH_LO_STEP)
    f->hash_entries_min = 0;
  else
    f->hash_entries_min = f->hash_size HASH_LO_MARK;
  DBG("Allocating OSPF hash of order %d: %d hash_entries, %d low, %d high\n",
      f->hash_order, f->hash_size, f->hash_entries_min, f->hash_entries_max);
  f->hash_table = mb_alloc(f->pool, f->hash_size * sizeof(struct top_hash_entry *));
  bzero(f->hash_table, f->hash_size * sizeof(struct top_hash_entry *));
}

static inline void
ospf_top_ht_free(struct top_hash_entry **h)
{
  mb_free(h);
}

static inline u32
ospf_top_hash_u32(u32 a)
{
  /* Shamelessly stolen from IP address hashing in ipv4.h */
  a ^= a >> 16;
  a ^= a << 10;
  return a;
}

static inline unsigned
ospf_top_hash(struct top_graph *f, u32 lsaid, u32 rtrid, u32 type)
{
  return (ospf_top_hash_u32(lsaid) + ospf_top_hash_u32(rtrid) + type) & f->hash_mask;
}

struct top_graph *
ospf_top_new(struct proto_ospf *p)
{
  struct top_graph *f;

  f = mb_allocz(p->proto.pool, sizeof(struct top_graph));
  f->pool = p->proto.pool;
  f->hash_slab = sl_new(f->pool, sizeof(struct top_hash_entry));
  f->hash_order = HASH_DEF_ORDER;
  ospf_top_ht_alloc(f);
  f->hash_entries = 0;
  f->hash_entries_min = 0;
  return f;
}

void
ospf_top_free(struct top_graph *f)
{
  rfree(f->hash_slab);
  ospf_top_ht_free(f->hash_table);
  mb_free(f);
}

static void
ospf_top_rehash(struct top_graph *f, int step)
{
  unsigned int oldn, oldh;
  struct top_hash_entry **n, **oldt, **newt, *e, *x;

  oldn = f->hash_size;
  oldt = f->hash_table;
  DBG("Re-hashing topology hash from order %d to %d\n", f->hash_order, f->hash_order+step);
  f->hash_order += step;
  ospf_top_ht_alloc(f);
  newt = f->hash_table;

  for(oldh=0; oldh < oldn; oldh++)
    {
      e = oldt[oldh];
      while (e)
	{
	  x = e->next;
	  n = newt + ospf_top_hash(f, e->lsa.id, e->lsa.rt, e->lsa.type);
	  e->next = *n;
	  *n = e;
	  e = x;
	}
    }
  ospf_top_ht_free(oldt);
}

struct top_hash_entry *
ospf_hash_find_header(struct top_graph *f, struct ospf_lsa_header *h)
{
  return ospf_hash_find(f,h->id,h->rt,h->type);
}
struct top_hash_entry *
ospf_hash_find(struct top_graph *f, u32 lsa, u32 rtr, u32 type)
{
  struct top_hash_entry *e = f->hash_table[ospf_top_hash(f, lsa, rtr, type)];

  while (e && (e->lsa.id != lsa || e->lsa.rt != rtr || e->lsa.type != type))
    e = e->next;
  return e;
}

struct top_hash_entry *
ospf_hash_get(struct top_graph *f, u32 lsa, u32 rtr, u32 type)
{
  struct top_hash_entry **ee = f->hash_table + ospf_top_hash(f, lsa, rtr, type);
  struct top_hash_entry *e = *ee;

  while (e && (e->lsa.id != lsa || e->lsa.rt != rtr || e->lsa.type != type))
    e = e->next;
  if (e)
    return e;
  e = sl_alloc(f->hash_slab);
  e->lsa.id = lsa;
  e->lsa.rt = rtr;
  e->lsa.type = type;
  e->lsa_body = NULL;
  e->next=*ee;		/* MJ you forgot this :-) */
  *ee=e;
  if (f->hash_entries++ > f->hash_entries_max)
    ospf_top_rehash(f, HASH_HI_STEP);
  return e;
}

void
ospf_hash_delete(struct top_graph *f, struct top_hash_entry *e)
{
  unsigned int h = ospf_top_hash(f, e->lsa.id, e->lsa.rt, e->lsa.type);
  struct top_hash_entry **ee = f->hash_table + h;

  while (*ee)
    {
      if (*ee == e)
	{
	  *ee = e->next;
	  sl_free(f->hash_slab, e);
	  if (f->hash_entries-- < f->hash_entries_min)
	    ospf_top_rehash(f, -HASH_LO_STEP);
	  return;
	}
      ee = &((*ee)->next);
    }
  bug("ospf_hash_delete() called for invalid node");
}

void
ospf_top_dump(struct top_graph *f)
{
  unsigned int i;
  debug("Hash entries: %d\n", f->hash_entries);

  for(i=0; i<f->hash_size; i++)
    {
      struct top_hash_entry *e = f->hash_table[i];
      while (e)
	{
	  debug("\t%04x %08x %08x %p\n", e->lsa.type, e->lsa.id,
            e->lsa.rt, e->lsa_body);
	  e = e->next;
	}
    }
}

