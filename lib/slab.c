/*
 *	BIRD Resource Manager -- SLABs
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdlib.h>
#include <string.h>

#include "nest/bird.h"
#include "lib/resource.h"

/*
 *	These are only fake, soon to change...
 */

struct sl_obj {
  node n;
  byte data[0];
};

struct slab {
  resource r;
  unsigned size;
  list objs;
};

void slab_free(resource *r);
void slab_dump(resource *r);

struct resclass sl_class = {
  "Slab",
  sizeof(struct slab),
  slab_free,
  slab_dump
};

slab *
sl_new(pool *p, unsigned size)
{
  slab *s = ralloc(p, &sl_class);
  s->size = size;
  init_list(&s->objs);
  return s;
}

void *
sl_alloc(slab *s)
{
  struct sl_obj *o = xmalloc(sizeof(struct sl_obj) + s->size);

  add_tail(&s->objs, &o->n);
  return o->data;
}

void
sl_free(slab *s, void *oo)
{
  struct sl_obj *o = SKIP_BACK(struct sl_obj, data, oo);

  rem_node(&o->n);
  xfree(o);
}

void
slab_free(resource *r)
{
  slab *s = (slab *) r;
  struct sl_obj *o, *p;

  for(o = HEAD(s->objs); p = (struct sl_obj *) o->n.next; o = p)
    xfree(o);
}

void
slab_dump(resource *r)
{
  slab *s = (slab *) r;
  int cnt = 0;
  struct sl_obj *o;

  WALK_LIST(o, s->objs)
    cnt++;
  debug("(%d objects per %d bytes)\n", cnt, s->size);
}
