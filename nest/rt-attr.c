/*
 *	BIRD -- Route Attribute Cache
 *
 *	(c) 1998--2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <alloca.h>

#include "nest/bird.h"
#include "nest/route.h"
#include "nest/protocol.h"
#include "nest/iface.h"
#include "nest/cli.h"
#include "lib/resource.h"
#include "lib/string.h"

static slab *rta_slab;
static pool *rta_pool;

struct protocol *attr_class_to_protocol[EAP_MAX];

/*
 *	Extended Attributes
 */

static inline eattr *
ea__find(ea_list *e, unsigned id)
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

eattr *
ea_find(ea_list *e, unsigned id)
{
  eattr *a = ea__find(e, id & EA_CODE_MASK);

  if (a && (a->type & EAF_TYPE_MASK) == EAF_TYPE_UNDEF &&
      !(id & EA_ALLOW_UNDEF))
    return NULL;
  return a;
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
  eattr *s, *d, *l;
  int i = 0;

  /* Discard duplicates and undefs. Do you remember sorting was stable? */
  s = d = e->attrs;
  l = e->attrs + e->count;
  while (s < l)
    {
      if ((s->type & EAF_TYPE_MASK) != EAF_TYPE_UNDEF)
	{
	  *d++ = *s;
	  i++;
	}
      s++;
      while (s < l && s->id == s[-1].id)
	s++;
    }
  e->count = i;
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
      if (e->count > 5)
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

int
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
      if (!(a->type & EAF_EMBEDDED))
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
ea_format(eattr *e, byte *buf)
{
  struct protocol *p;
  int status = GA_UNKNOWN;
  unsigned int i, l;
  struct adata *ad = (e->type & EAF_EMBEDDED) ? NULL : e->u.ptr;

  if (p = attr_class_to_protocol[EA_PROTO(e->id)])
    {
      buf += bsprintf(buf, "%s.", p->name);
      if (p->get_attr)
	status = p->get_attr(e, buf);
      buf += strlen(buf);
    }
  else if (EA_PROTO(e->id))
    buf += bsprintf(buf, "%02x.", EA_PROTO(e->id));
  if (status < GA_NAME)
    buf += bsprintf(buf, "%02x", EA_ID(e->id));
  if (status < GA_FULL)
    {
      *buf++ = ':';
      *buf++ = ' ';
      switch (e->type & EAF_TYPE_MASK)
	{
	case EAF_TYPE_INT:
	  bsprintf(buf, "%d", e->u.data);
	  break;
	case EAF_TYPE_OPAQUE:
	  l = (ad->length < 16) ? ad->length : 16;
	  for(i=0; i<l; i++)
	    {
	      buf += bsprintf(buf, "%02x", ad->data[i]);
	      if (i < l)
		*buf++ = ' ';
	    }
	  if (l < ad->length)
	    strcpy(buf, "...");
	  break;
	case EAF_TYPE_IP_ADDRESS:
	  bsprintf(buf, "%I", *(ip_addr *) ad->data);
	  break;
	case EAF_TYPE_ROUTER_ID:
	  bsprintf(buf, "%08x", e->u.data); /* FIXME: Better printing of router ID's */
	  break;
	case EAF_TYPE_AS_PATH:		/* FIXME */
	case EAF_TYPE_INT_SET:		/* FIXME */
	case EAF_TYPE_UNDEF:
	default:
	  bsprintf(buf, "<type %02x>", e->type);
	}
    }
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
	  if (a->type & EAF_TEMP)
	    debug("T");
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

inline unsigned int
ea_hash(ea_list *e)
{
  u32 h = 0;
  int i;

  if (e)			/* Assuming chain of length 1 */
    {
      for(i=0; i<e->count; i++)
	{
	  struct eattr *a = &e->attrs[i];
	  h ^= a->id;
	  if (a->type & EAF_EMBEDDED)
	    h ^= a->u.data;
	  else
	    {
	      struct adata *d = a->u.ptr;
	      int size = d->length;
	      byte *z = d->data;
	      while (size >= 4)
		{
		  h ^= *(u32 *)z;
		  z += 4;
		  size -= 4;
		}
	      while (size--)
		h = (h >> 24) ^ (h << 8) ^ *z++;
	    }
	}
      h ^= h >> 16;
      h ^= h >> 6;
      h &= 0xffff;
    }
  return h;
}

/*
 *	rta's
 */

static unsigned int rta_cache_count;
static unsigned int rta_cache_size = 32;
static unsigned int rta_cache_limit;
static unsigned int rta_cache_mask;
static rta **rta_hash_table;

static void
rta_alloc_hash(void)
{
  rta_hash_table = mb_alloc(rta_pool, sizeof(rta *) * rta_cache_size);
  bzero(rta_hash_table, sizeof(rta *) * rta_cache_size);
  if (rta_cache_size < 32768)
    rta_cache_limit = rta_cache_size * 2;
  else
    rta_cache_limit = ~0;
  rta_cache_mask = rta_cache_size - 1;
}

static inline unsigned int
rta_hash(rta *a)
{
  return a->proto->hash_key ^ ipa_hash(a->gw) ^ ea_hash(a->eattrs);
}

static inline int
rta_same(rta *x, rta *y)
{
  return (x->proto == y->proto &&
	  x->source == y->source &&
	  x->scope == y->scope &&
	  x->cast == y->cast &&
	  x->dest == y->dest &&
	  x->flags == y->flags &&
	  ipa_equal(x->gw, y->gw) &&
	  ipa_equal(x->from, y->from) &&
	  x->iface == y->iface &&
	  ea_same(x->eattrs, y->eattrs));
}

static rta *
rta_copy(rta *o)
{
  rta *r = sl_alloc(rta_slab);

  memcpy(r, o, sizeof(rta));
  r->uc = 1;
  r->eattrs = ea_list_copy(o->eattrs);
  return r;
}

static inline void
rta_insert(rta *r)
{
  unsigned int h = r->hash_key & rta_cache_mask;
  r->next = rta_hash_table[h];
  if (r->next)
    r->next->pprev = &r->next;
  r->pprev = &rta_hash_table[h];
  rta_hash_table[h] = r;
}

static void
rta_rehash(void)
{
  unsigned int ohs = rta_cache_size;
  unsigned int h;
  rta *r, *n;
  rta **oht = rta_hash_table;

  rta_cache_size = 2*rta_cache_size;
  DBG("Rehashing rta cache from %d to %d entries.\n", ohs, rta_cache_size);
  rta_alloc_hash();
  for(h=0; h<ohs; h++)
    for(r=oht[h]; r; r=n)
      {
	n = r->next;
	rta_insert(r);
      }
  mb_free(oht);
}

rta *
rta_lookup(rta *o)
{
  rta *r;
  unsigned int h;

  ASSERT(!(o->aflags & RTAF_CACHED));
  if (o->eattrs)
    {
      if (o->eattrs->next)	/* Multiple ea_list's, need to merge them */
	{
	  ea_list *ml = alloca(ea_scan(o->eattrs));
	  ea_merge(o->eattrs, ml);
	  o->eattrs = ml;
	}
      ea_sort(o->eattrs);
    }

  h = rta_hash(o);
  for(r=rta_hash_table[h & rta_cache_mask]; r; r=r->next)
    if (r->hash_key == h && rta_same(r, o))
      return rta_clone(r);

  r = rta_copy(o);
  r->hash_key = h;
  r->aflags = RTAF_CACHED;
  rta_insert(r);

  if (++rta_cache_count > rta_cache_limit)
    rta_rehash();

  return r;
}

void
rta__free(rta *a)
{
  ASSERT(rta_cache_count && (a->aflags & RTAF_CACHED));
  rta_cache_count--;
}

void
rta_dump(rta *a)
{
  static char *rts[] = { "RTS_DUMMY", "RTS_STATIC", "RTS_INHERIT", "RTS_DEVICE",
			 "RTS_STAT_DEV", "RTS_REDIR", "RTS_RIP", "RTS_RIP_EXT",
			 "RTS_OSPF", "RTS_OSPF_EXT", "RTS_OSPF_IA",
			 "RTS_OSPF_BOUNDARY", "RTS_BGP" };
  static char *rtc[] = { "", " BC", " MC", " AC" };
  static char *rtd[] = { "", " DEV", " HOLE", " UNREACH", " PROHIBIT" };

  debug("p=%s uc=%d %s %s%s%s h=%04x",
	a->proto->name, a->uc, rts[a->source], ip_scope_text(a->scope), rtc[a->cast],
	rtd[a->dest], a->hash_key);
  if (!(a->aflags & RTAF_CACHED))
    debug(" !CACHED");
  debug(" <-%I", a->from);
  if (a->dest == RTD_ROUTER)
    debug(" ->%I", a->gw);
  if (a->dest == RTD_DEVICE || a->dest == RTD_ROUTER)
    debug(" [%s]", a->iface ? a->iface->name : "???" );
  if (a->eattrs)
    {
      debug(" EA: ");
      ea_dump(a->eattrs);
    }
}

void
rta_dump_all(void)
{
  rta *a;
  unsigned int h;

  debug("Route attribute cache (%d entries, rehash at %d):\n", rta_cache_count, rta_cache_limit);
  for(h=0; h<rta_cache_size; h++)
    for(a=rta_hash_table[h]; a; a=a->next)
      {
	debug("%p ", a);
	rta_dump(a);
	debug("\n");
      }
  debug("\n");
}

void
rta_show(struct cli *c, rta *a)
{
  static char *src_names[] = { "dummy", "static", "inherit", "device", "static-device", "redirect",
			       "RIP", "RIP-ext", "OSPF", "OSPF-ext", "OSPF-IA", "OSPF-boundary",
			       "BGP" };
  static char *cast_names[] = { "unicast", "broadcast", "multicast", "anycast" };
  ea_list *eal;
  int i;
  byte buf[256];

  cli_printf(c, -1008, "\tType: %s %s %s", src_names[a->source], cast_names[a->cast], ip_scope_text(a->scope));
  for(eal=a->eattrs; eal; eal=eal->next)
    for(i=0; i<eal->count; i++)
      {
	ea_format(&eal->attrs[i], buf);
	cli_printf(c, -1012, "\t%s", buf);
      }
}

void
rta_init(void)
{
  rta_pool = rp_new(&root_pool, "Attributes");
  rta_slab = sl_new(rta_pool, sizeof(rta));
  rta_alloc_hash();
}
