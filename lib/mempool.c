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

struct mp_chunk {
  struct mp_chunk *next;
  byte data[0];
};

struct mempool {
  resource r;
  byte *ptr, *end;
  struct mp_chunk *first, **plast;
  unsigned chunk_size, threshold, total;
};

void mp_free(resource *);
void mp_dump(resource *);

struct resclass mp_class = {
  "MemPool",
  sizeof(struct mempool),
  mp_free,
  mp_dump
};

mempool
*mp_new(pool *p, unsigned blk)
{
  mempool *m = ralloc(p, &mp_class);
  m->ptr = m->end = NULL;
  m->first = NULL;
  m->plast = &m->first;
  m->chunk_size = blk;
  m->threshold = 3*blk/4;
  m->total = 0;
  return m;
}

void *
mp_alloc(mempool *m, unsigned size)
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
      struct mp_chunk *c;
      if (size >= m->threshold)
	{
	  c = xmalloc(sizeof(struct mp_chunk) + size);
	  m->total += size;
	}
      else
	{
	  c = xmalloc(sizeof(struct mp_chunk) + m->chunk_size);
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
mp_allocu(mempool *m, unsigned size)
{
  byte *a = m->ptr;
  byte *e = a + size;

  if (e <= m->end)
    {
      m->ptr = e;
      return a;
    }
  return mp_alloc(m, size);
}

void *
mp_allocz(mempool *m, unsigned size)
{
  void *z = mp_alloc(m, size);

  bzero(z, size);
  return z;
}

void
mp_free(resource *r)
{
  mempool *m = (mempool *) r;
  struct mp_chunk *c, *d;

  for(d=m->first; d; d = c)
    {
      c = d->next;
      xfree(d);
    }
}

void
mp_dump(resource *r)
{
  mempool *m = (mempool *) r;
  struct mp_chunk *c;
  int cnt;

  for(cnt=0, c=m->first; c; c=c->next, cnt++)
    ;
  debug("(chunk=%d threshold=%d count=%d total=%d)\n",
	m->chunk_size,
	m->threshold,
	cnt,
	m->total);
}
