/*
 *	BIRD Library -- Linked Lists
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_LISTS_H_
#define _BIRD_LISTS_H_

typedef struct node {
  struct node *next, *prev;
} node;

typedef struct list {			/* In fact two overlayed nodes */
  struct node *head, *null, *tail;
} list;

#define NODE (node *)
#define HEAD(list) ((void *)((list).head))
#define TAIL(list) ((void *)((list).tail))
#define WALK_LIST(n,list) for((n)=HEAD(list);(NODE (n))->next; \
				n=(void *)((NODE (n))->next))
#define EMPTY_LIST(list) (!(list).head->next)

void add_tail(list *, node *);
void add_head(list *, node *);
void rem_node(node *);
void add_tail_list(list *, list *);
void init_list(list *);
void insert_node(node *, node *);

#ifndef _BIRD_LISTS_C_
#define LIST_INLINE extern inline
#include "lib/lists.c"
#undef LIST_INLINE
#else
#define LIST_INLINE
#endif

#endif
