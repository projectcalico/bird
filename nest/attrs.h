/*
 *	BIRD Internet Routing Daemon -- Attribute Operations
 *
 *	(c) 2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_ATTRS_H_
#define _BIRD_ATTRS_H_

/* a-path.c */

struct adata *as_path_prepend(struct linpool *pool, struct adata *olda, int as);
void as_path_format(struct adata *path, byte *buf, unsigned int size);

#define AS_PATH_SET		1	/* Types of path segments */
#define AS_PATH_SEQUENCE	2

/* a-set.c */

void int_set_format(struct adata *set, byte *buf, unsigned int size);

#endif
