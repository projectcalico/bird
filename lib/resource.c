/*
 *	BIRD Resource Manager
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <stdlib.h>

#include "nest/bird.h"
#include "lib/resource.h"

struct pool {
  resource r;
  list inside;
};

void pool_dump(resource *);
void pool_free(resource *);

static struct resclass pool_class = {
  "Pool",
  sizeof(pool),
  pool_free,
  pool_dump
};

pool root_pool;

static int indent;

pool *
rp_new(pool *p)
{
  pool *z = ralloc(p, &pool_class);
  init_list(&z->inside);
  return z;
}

void
pool_free(resource *P)
{
  pool *p = (pool *) P;
  resource *r, *rr;

  r = HEAD(p->inside);
  while (rr = (resource *) r->n.next)
    {
      r->class->free(r);
      xfree(r);
      r = rr;
    }
}

void
pool_dump(resource *P)
{
  pool *p = (pool *) P;
  resource *r;

  debug("\n");
  indent += 3;
  WALK_LIST(r, p->inside)
    rdump(r);
  indent -= 3;
}

void
rfree(void *res)
{
  resource *r = res;

  if (r)
    {
      if (r->n.next)
	rem_node(&r->n);
      r->class->free(r);
      xfree(r);
    }
}

void
rdump(void *res)
{
  char x[16];
  resource *r = res;

  sprintf(x, "%%%ds%%08x ", indent);
  debug(x, "", (int) r);
  if (r)
    {
      debug("%-6s", r->class->name);
      r->class->dump(r);
    }
  else
    debug("NULL\n");
}

void *
ralloc(pool *p, struct resclass *c)
{
  resource *r = xmalloc(c->size);

  r->class = c;
  add_tail(&p->inside, &r->n);
  return r;
}

void
resource_init(void)
{
  root_pool.r.class = &pool_class;
  init_list(&root_pool.inside);
}

/*
 *	Memory blocks.
 */

struct mblock {
  resource r;
  unsigned size;
  byte data[0];
};

void mbl_free(resource *r)
{
}

void mbl_debug(resource *r)
{
  struct mblock *m = (struct mblock *) r;

  debug("(size=%d)\n", m->size);
}

struct resclass mb_class = {
  "Memory",
  0,
  mbl_free,
  mbl_debug,
};

void *
mb_alloc(pool *p, unsigned size)
{
  struct mblock *b = xmalloc(sizeof(struct mblock) + size);

  b->r.class = &mb_class;
  add_tail(&p->inside, &b->r.n);
  b->size = size;
  return b->data;
}

void
mb_free(void *m)
{
  struct mblock *b = SKIP_BACK(struct mblock, data, m);
  rfree(b);
}
