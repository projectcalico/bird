/*
 *	BIRD -- Forwarding Information Base -- Data Structures
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include <string.h>

#include "nest/bird.h"
#include "nest/route.h"

#define HASH_DEF_SIZE 1024
#define HASH_HI_MARK *16
#define HASH_LO_MARK *0
#define HASH_LO_MIN 128
#define HASH_HI_RESIZE *16
#define HASH_LO_RESIZE *0

static void
fib_ht_alloc(struct fib *f)
{
  f->hash_mask = f->hash_size - 1;
  f->entries_max = f->hash_size HASH_HI_MARK;
  f->entries_min = f->hash_size HASH_LO_MARK;
  if (f->entries_min < HASH_LO_MIN)
    f->entries_min = 0;
  DBG("Allocating FIB: %d entries, %d low, %d high\n", f->hash_size, f->entries_min, f->entries_max);
  f->hash_table = mb_alloc(f->fib_pool, f->hash_size * sizeof(struct fib_node *));
  bzero(f->hash_table, f->hash_size * sizeof(struct fib_node *));
}

static inline void
fib_ht_free(struct fib_node **h)
{
  mb_free(h);
}

static inline unsigned
fib_hash(struct fib *f, ip_addr *a)
{
  return ipa_hash(*a) & f->hash_mask;
}

void
fib_init(struct fib *f, pool *p, unsigned node_size, unsigned hash_size, void (*init)(struct fib_node *))
{
  if (!hash_size)
    hash_size = HASH_DEF_SIZE;
  f->fib_pool = p;
  f->fib_slab = sl_new(p, node_size);
  f->hash_size = hash_size;
  fib_ht_alloc(f);
  f->entries = 0;
  f->entries_min = 0;
  f->init = init;
}

static void
fib_rehash(struct fib *f, unsigned new)
{
  unsigned old;
  struct fib_node **n, *e, *x, **t, **m, **h;

  old = f->hash_size;
  m = h = f->hash_table;
  DBG("Re-hashing FIB from %d to %d\n", old, new);
  f->hash_size = new;
  fib_ht_alloc(f);
  n = f->hash_table;
  while (old--)
    {
      x = *h++;
      while (e = x)
	{
	  x = e->next;
	  t = n + fib_hash(f, &e->prefix);
	  e->next = *t;
	  *t = e;
	}
    }
  fib_ht_free(m);
}

void *
fib_find(struct fib *f, ip_addr *a, int len)
{
  struct fib_node *e = f->hash_table[fib_hash(f, a)];

  while (e && (e->pxlen != len || !ipa_equal(*a, e->prefix)))
    e = e->next;
  return e;
}

void *
fib_get(struct fib *f, ip_addr *a, int len)
{
  struct fib_node **ee = f->hash_table + fib_hash(f, a);
  struct fib_node *e = *ee;

  while (e && (e->pxlen != len || !ipa_equal(*a, e->prefix)))
    e = e->next;
  if (e)
    return e;
#ifdef DEBUG
  if (len < 0 || len > BITS_PER_IP_ADDRESS || !ip_is_prefix(*a,len))
    die("fib_get() called for invalid address");
#endif
  e = sl_alloc(f->fib_slab);
  e->prefix = *a;
  e->pxlen = len;
  e->next = *ee;
  *ee = e;
  f->init(e);
  if (f->entries++ > f->entries_max)
    fib_rehash(f, f->hash_size HASH_HI_RESIZE);
  return e;
}

void
fib_delete(struct fib *f, void *E)
{
  struct fib_node *e = E;
  struct fib_node **ee = f->hash_table + fib_hash(f, &e->prefix);

  while (*ee)
    {
      if (*ee == e)
	{
	  *ee = e->next;
	  sl_free(f->fib_slab, e);
	  if (f->entries-- < f->entries_min)
	    fib_rehash(f, f->hash_size HASH_LO_RESIZE);
	  return;
	}
      ee = &((*ee)->next);
    }
  die("fib_delete() called for invalid node");
}

void
fib_free(struct fib *f)
{
  fib_ht_free(f->hash_table);
  rfree(f->fib_slab);
}
