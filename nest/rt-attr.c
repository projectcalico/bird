/*
 *	BIRD -- Route Attribute Cache
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <string.h>
#include <alloca.h>

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

/*
 *	Extended Attributes
 */

eattr *
ea_find(ea_list *e, unsigned id)
{
  eattr *a;
  int l, r, m;

  while (e)
    {
      if (e->flags & EALF_BISECT)
	{
	  l = 0;
	  r = e->count + 1;
	  while (l <= r)
	    {
	      m = (l+r) / 2;
	      a = &e->attrs[m];
	      if (a->id == id)
		return a;
	      else if (a->id < id)
		l = m+1;
	      else
		r = m-1;
	    }
	}
      else
	for(m=0; m<e->count; m++)
	  if (e->attrs[m].id == id)
	    return &e->attrs[m];
      e = e->next;
    }
  return NULL;
}

static inline void
ea_do_sort(ea_list *e)
{
  unsigned n = e->count;
  eattr *a = e->attrs;
  eattr *b = alloca(n * sizeof(eattr));
  unsigned s, ss;

  /* We need to use a stable sorting algorithm, hence mergesort */
  do
    {
      s = ss = 0;
      while (s < n)
	{
	  eattr *p, *q, *lo, *hi;
	  p = b;
	  ss = s;
	  *p++ = a[s++];
	  while (s < n && p[-1].id <= a[s].id)
	    *p++ = a[s++];
	  if (s < n)
	    {
	      q = p;
	      *p++ = a[s++];
	      while (s < n && p[-1].id <= a[s].id)
		*p++ = a[s++];
	      lo = b;
	      hi = q;
	      s = ss;
	      while (lo < q && hi < p)
		if (lo->id <= hi->id)
		  a[s++] = *lo++;
		else
		  a[s++] = *hi++;
	      while (lo < q)
		a[s++] = *lo++;
	      while (hi < p)
		a[s++] = *hi++;
	    }
	}
    }
  while (ss);
}

static inline void
ea_do_prune(ea_list *e)
{
  eattr *s, *d;
  int i;

  /* Discard duplicates. Do you remember sorting was stable? */
  s = d = e->attrs + 1;
  for(i=1; i<e->count; i++)
    if (s->id != d[-1].id)
      *d++ = *s++;
    else
      s++;
  e->count = d - e->attrs;
}

void
ea_sort(ea_list *e)
{
  while (e)
    {
      if (!(e->flags & EALF_SORTED))
	{
	  ea_do_sort(e);
	  ea_do_prune(e);
	  e->flags |= EALF_SORTED;
	}
#if 0			/* FIXME: Remove this after some testing */
      if (e->count > 5)
#endif
	e->flags |= EALF_BISECT;
      e = e->next;
    }
}

unsigned
ea_scan(ea_list *e)
{
  unsigned cnt = 0;

  while (e)
    {
      cnt += e->count;
      e = e->next;
    }
  return sizeof(ea_list) + sizeof(eattr)*cnt;
}

void
ea_merge(ea_list *e, ea_list *t)
{
  eattr *d = t->attrs;

  t->flags = 0;
  t->count = 0;
  t->next = NULL;
  while (e)
    {
      memcpy(d, e->attrs, sizeof(eattr)*e->count);
      t->count += e->count;
      d += e->count;
      e = e->next;
    }
}

static inline int
ea_same(ea_list *x, ea_list *y)
{
  int c;

  if (!x || !y)
    return x == y;
  ASSERT(!x->next && !y->next);
  if (x->count != y->count)
    return 0;
  for(c=0; c<x->count; c++)
    {
      eattr *a = &x->attrs[c];
      eattr *b = &y->attrs[c];

      if (a->id != b->id ||
	  a->flags != b->flags ||
	  a->type != b->type ||
	  ((a->type & EAF_EMBEDDED) ? a->u.data != b->u.data :
	   (a->u.ptr->length != b->u.ptr->length || memcmp(a->u.ptr, b->u.ptr, a->u.ptr->length))))
	return 0;
    }
  return 1;
}

static inline ea_list *
ea_list_copy(ea_list *o)
{
  ea_list *n;
  unsigned i, len;

  if (!o)
    return NULL;
  ASSERT(!o->next);
  len = sizeof(ea_list) + sizeof(eattr) * o->count;
  n = mb_alloc(rta_pool, len);
  memcpy(n, o, len);
  n->flags |= EALF_CACHED;
  for(i=0; i<o->count; i++)
    {
      eattr *a = &n->attrs[i];
      if (!(a->flags & EAF_EMBEDDED))
	{
	  unsigned size = sizeof(struct adata) + a->u.ptr->length;
	  struct adata *d = mb_alloc(rta_pool, size);
	  memcpy(d, a->u.ptr, size);
	  a->u.ptr = d;
	}
    }
  return n;
}

void
ea_dump(ea_list *e)
{
  int i;

  if (!e)
    {
      debug("NONE");
      return;
    }
  while (e)
    {
      debug("[%c%c%c]",
	    (e->flags & EALF_SORTED) ? 'S' : 's',
	    (e->flags & EALF_BISECT) ? 'B' : 'b',
	    (e->flags & EALF_CACHED) ? 'C' : 'c');
      for(i=0; i<e->count; i++)
	{
	  eattr *a = &e->attrs[i];
	  debug(" %02x:%02x.%02x", EA_PROTO(a->id), EA_ID(a->id), a->flags);
	  if (a->type & EAF_INLINE)
	    debug("*");
	  debug("=%c", "?iO?I?P???S?????" [a->type & EAF_TYPE_MASK]);
	  if (a->type & EAF_EMBEDDED)
	    debug(":%08x", a->u.data);
	  else
	    {
	      int j, len = a->u.ptr->length;
	      debug("[%d]:", len);
	      for(j=0; j<len; j++)
		debug("%02x", a->u.ptr->data[j]);
	    }
	}
      if (e = e->next)
	debug(" | ");
    }
}

/*
 *	rta's
 */

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
	  ea_same(x->attrs, y->attrs));
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

  ASSERT(!(o->aflags & RTAF_CACHED));
  if (o->attrs)
    {
      if (o->attrs->next)	/* Multiple ea_list's, need to merge them */
	{
	  ea_list *ml = alloca(ea_scan(o->attrs));
	  ea_merge(o->attrs, ml);
	  o->attrs = ml;
	}
      ea_sort(o->attrs);
    }

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
rta__free(rta *a)
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
	a->proto->name, a->uc, rts[a->source], sco[a->scope], rtc[a->cast],
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
  if (a->attrs)
    {
      debug(" EA: ");
      ea_dump(a->attrs);
    }
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
