/*
 *	BIRD -- Route Attribute Cache
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <string.h>

#include "nest/bird.h"
#include "nest/route.h"
#include "nest/protocol.h"
#include "nest/iface.h"
#include "lib/resource.h"

/*
 *	FIXME: Implement hash tables and garbage collection!
 */

static rta *first_rta;
static slab *rta_slab;
static pool *rta_pool;

static inline int
ea_same(ea_list *x, ea_list *y)
{
  int c;

  while (x && y)
    {
      if (x->nattrs != y->nattrs)
	return 0;
      for(c=0; c<x->nattrs; c++)
	{
	  eattr *a = &x->attrs[c];
	  eattr *b = &y->attrs[c];

	  if (a->protocol != b->protocol ||
	      a->flags != b->flags ||
	      a->id != b->id ||
	      ((a->flags & EAF_LONGWORD) ? a->u.data != b->u.data :
	       (a->u.ptr->length != b->u.ptr->length || memcmp(a->u.ptr, b->u.ptr, a->u.ptr->length))))
	    return 0;
	}
      x = x->next;
      y = y->next;
    }
  return (!x && !y);
}

static inline int
rta_same(rta *x, rta *y)
{
  return (x->proto == y->proto &&
	  x->source == y->source &&
	  x->scope == y->scope &&
	  x->cast == y->cast &&
	  x->dest == y->dest &&
	  x->tos == y->tos &&
	  x->flags == y->flags &&
	  ipa_equal(x->gw, y->gw) &&
	  ipa_equal(x->from, y->from) &&
	  x->iface == y->iface &&
	  ea_same(x->attrs, y->attrs) &&
	  (!x->proto->rta_same || x->proto->rta_same(x, y)));
}

static inline ea_list *
ea_list_copy(ea_list *o)
{
  ea_list *n, **p, *z;
  unsigned i;

  p = &n;
  while (o)
    {
      z = mb_alloc(rta_pool, sizeof(ea_list) + sizeof(eattr) * o->nattrs);
      memcpy(z, o, sizeof(ea_list) + sizeof(eattr) * o->nattrs);
      *p = z;
      p = &z->next;
      for(i=0; i<o->nattrs; i++)
	{
	  eattr *a = o->attrs + i;
	  if (!(a->flags & EAF_LONGWORD))
	    {
	      unsigned size = sizeof(struct adata) + a->u.ptr->length;
	      struct adata *d = mb_alloc(rta_pool, size);
	      memcpy(d, a->u.ptr, size);
	      a->u.ptr = d;
	    }
	}
      o = o->next;
    }
  *p = NULL;
  return n;
}

static rta *
rta_copy(rta *o)
{
  rta *r = sl_alloc(rta_slab);

  memcpy(r, o, sizeof(rta));
  r->uc = 1;
  r->attrs = ea_list_copy(o->attrs);
  return r;
}

rta *
rta_lookup(rta *o)
{
  rta *r;

  for(r=first_rta; r; r=r->next)
    if (rta_same(r, o))
      return rta_clone(r);
  r = rta_copy(o);
  r->aflags = RTAF_CACHED;
  r->next = first_rta;
  first_rta = r;
  return r;
}

void
_rta_free(rta *a)
{
}

void
rta_dump(rta *a)
{
  static char *rts[] = { "RTS_DUMMY", "RTS_STATIC", "RTS_INHERIT", "RTS_DEVICE",
			 "RTS_STAT_DEV", "RTS_REDIR", "RTS_RIP", "RTS_RIP_EXT",
			 "RTS_OSPF", "RTS_OSPF_EXT", "RTS_OSPF_IA",
			 "RTS_OSPF_BOUNDARY", "RTS_BGP" };
  static char *sco[] = { "HOST", "LINK", "SITE", "UNIV" };
  static char *rtc[] = { "", " BC", " MC", " AC" };
  static char *rtd[] = { "", " DEV", " HOLE", " UNREACH", " PROHIBIT" };

  debug("p=%s uc=%d %s %s%s%s TOS=%d",
	a->proto->cf->name, a->uc, rts[a->source], sco[a->scope], rtc[a->cast],
	rtd[a->dest], a->tos);
  if (a->flags & RTF_EXTERIOR)
    debug(" EXT");
  if (a->flags & RTF_TAGGED)
    debug(" TAG");
  if (!(a->aflags & RTAF_CACHED))
    debug(" !CACHED");
  debug(" <-%I", a->from);
  if (a->dest == RTD_ROUTER)
    debug(" ->%I", a->gw);
  if (a->dest == RTD_DEVICE || a->dest == RTD_ROUTER)
    debug(" [%s]", a->iface ? a->iface->name : "???" );
}

void
rta_dump_all(void)
{
  rta *a;

  debug("Route attribute cache:\n");
  for(a=first_rta; a; a=a->next)
    {
      debug("%p ", a);
      rta_dump(a);
      debug("\n");
    }
  debug("\n");
}

void
rta_init(void)
{
  rta_pool = rp_new(&root_pool, "Attributes");
  rta_slab = sl_new(rta_pool, sizeof(rta));
}
