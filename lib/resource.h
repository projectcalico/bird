/*
 *	BIRD Resource Manager
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_RESOURCE_H_
#define _BIRD_RESOURCE_H_

#include "lib/lists.h"

/* Resource */

typedef struct resource {
	node n;				/* Inside resource pool */
	struct resclass *class;		/* Resource class */
} resource;

/* Resource class */

struct resclass {
	char *name;			/* Resource class name */
	unsigned size;			/* Standard size of single resource */
	void (*free)(resource *);	/* Freeing function */
	void (*dump)(resource *);	/* Dump to debug output */
};

/* Generic resource manipulation */

typedef struct pool pool;

pool *rp_new(pool *);			/* Create new pool */
void rp_init(pool *);			/* Initialize static pool */
void rp_empty(pool *);			/* Free everything in the pool */
void rfree(void *);			/* Free single resource */
void rdump(void *);			/* Dump to debug output */

void ralloc(pool *, struct resclass *);

/* Normal memory blocks */

void *mb_alloc(pool *, unsigned size);
void *mb_free(void *);

/* Memory pools with linear allocation */

typedef struct mempool mempool;

mempool *mp_new(pool *, unsigned blk);
void mp_trim(pool *);				/* Free unused memory */
void *mp_alloc(mempool *, unsigned size);	/* Aligned */
void *mp_allocu(mempool *, unsigned size);	/* Unaligned */
void *mp_allocz(mempool *, unsigned size);	/* With clear */

/* Slabs */

typedef struct slab slab;

slab *sl_new(pool *, unsigned size);
void *sl_alloc(slab *);
void sl_free(slab *, void *);

/* Low-level memory allocation functions, please don't use */

void *xmalloc(unsigned);
#define xfree(x) free(x)

#endif
