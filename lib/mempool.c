/*
 *	BIRD Resource Manager -- Memory Pools
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdlib.h>
#include <string.h>

#include "nest/bird.h"
#include "lib/resource.h"

struct lp_chunk {
  struct lp_chunk *next;
  byte data[0];
};

struct linpool {
  resource r;
  byte *ptr, *end;
  struct lp_chunk *first, **plast;
  unsigned chunk_size, threshold, total;
};

void lp_free(resource *);
void lp_dump(resource *);

static struct resclass lp_class = {
  "LinPool",
  sizeof(struct linpool),
  lp_free,
  lp_dump
};

linpool
*lp_new(pool *p, unsigned blk)
{
  linpool *m = ralloc(p, &lp_class);
  m->ptr = m->end = NULL;
  m->first = NULL;
  m->plast = &m->first;
  m->chunk_size = blk;
  m->threshold = 3*blk/4;
  m->total = 0;
  return m;
}

void *
lp_alloc(linpool *m, unsigned size)
{
  byte *a = (byte *) ALIGN((unsigned long) m->ptr, CPU_STRUCT_ALIGN);
  byte *e = a + size;

  if (e <= m->end)
    {
      m->ptr = e;
      return a;
    }
  else
    {
      struct lp_chunk *c;
      if (size >= m->threshold)
	{
	  c = xmalloc(sizeof(struct lp_chunk) + size);
	  m->total += size;
	}
      else
	{
	  c = xmalloc(sizeof(struct lp_chunk) + m->chunk_size);
	  m->ptr = c->data + size;
	  m->end = c->data + m->chunk_size;
	  m->total += m->chunk_size;
	}
      *m->plast = c;
      m->plast = &c->next;
      c->next = NULL;
      return c->data;
    }
}

void *
lp_allocu(linpool *m, unsigned size)
{
  byte *a = m->ptr;
  byte *e = a + size;

  if (e <= m->end)
    {
      m->ptr = e;
      return a;
    }
  return lp_alloc(m, size);
}

void *
lp_allocz(linpool *m, unsigned size)
{
  void *z = lp_alloc(m, size);

  bzero(z, size);
  return z;
}

void
lp_free(resource *r)
{
  linpool *m = (linpool *) r;
  struct lp_chunk *c, *d;

  for(d=m->first; d; d = c)
    {
      c = d->next;
      xfree(d);
    }
}

void
lp_dump(resource *r)
{
  linpool *m = (linpool *) r;
  struct lp_chunk *c;
  int cnt;

  for(cnt=0, c=m->first; c; c=c->next, cnt++)
    ;
  debug("(chunk=%d threshold=%d count=%d total=%d)\n",
	m->chunk_size,
	m->threshold,
	cnt,
	m->total);
}
