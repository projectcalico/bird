/*
 *	BIRD -- OSPF Topological Database
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 *	(c) 1999 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include <string.h>

#include "nest/bird.h"

#include "ospf.h"

#define HASH_DEF_ORDER 6		/* FIXME: Increase */
#define HASH_HI_MARK *4
#define HASH_HI_STEP 2
#define HASH_HI_MAX 16
#define HASH_LO_MARK /5
#define HASH_LO_STEP 2
#define HASH_LO_MIN 8

void
addifa_rtlsa(struct ospf_iface *ifa)
{
  struct ospf_area *oa;
  struct proto_ospf *po;

  po=ifa->proto;
  oa=po->firstarea;

  while(oa!=NULL)
  {
    if(oa->areaid==ifa->area) break;
    oa=oa->next;
  }
 
  if(oa!=NULL)	/* Known area */
  {
    /**/;
  }
  else	/* New area */
  {
    po->areano++;
    oa=po->firstarea;
    po->firstarea=sl_alloc(po->areaslab);
    po->firstarea->next=oa;
    po->firstarea->areaid=ifa->area;
    po->firstarea->gr=ospf_top_new(po);
  }

  /* FIXME Go on, change router lsa, bits and so on... */
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
	  n = newt + ospf_top_hash(f, e->lsa_id, e->rtr_id, e->lsa_type);
	  e->next = *n;
	  *n = e;
	  e = x;
	}
    }
  ospf_top_ht_free(oldt);
}

struct top_hash_entry *
ospf_hash_find(struct top_graph *f, u32 lsa, u32 rtr, u32 type)
{
  struct top_hash_entry *e = f->hash_table[ospf_top_hash(f, lsa, rtr, type)];

  while (e && (e->lsa_id != lsa || e->rtr_id != rtr || e->lsa_type != type))
    e = e->next;
  return e;
}

struct top_hash_entry *
ospf_hash_get(struct top_graph *f, u32 lsa, u32 rtr, u32 type)
{
  struct top_hash_entry **ee = f->hash_table + ospf_top_hash(f, lsa, rtr, type);
  struct top_hash_entry *e = *ee;

  while (e && (e->lsa_id != lsa || e->rtr_id != rtr || e->lsa_type != type))
    e = e->next;
  if (e)
    return e;
  e = sl_alloc(f->hash_slab);
  e->lsa_id = lsa;
  e->rtr_id = rtr;
  e->lsa_type = type;
  e->vertex = NULL;
  if (f->hash_entries++ > f->hash_entries_max)
    ospf_top_rehash(f, HASH_HI_STEP);
  return e;
}

void
ospf_hash_delete(struct top_graph *f, struct top_hash_entry *e)
{
  unsigned int h = ospf_top_hash(f, e->lsa_id, e->rtr_id, e->lsa_type);
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

  for(i=0; i<f->hash_size; i++)
    {
      struct top_hash_entry *e = f->hash_table[i];
      while (e)
	{
	  debug("%04x %08x %08x %p\n", e->lsa_type, e->lsa_id, e->rtr_id, e->vertex);
	  e = e->next;
	}
    }
}
