/*
 *	BIRD Library -- Linked Lists
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define _BIRD_LISTS_C_

#include <nest/bird.h>
#include <lib/lists.h>

LIST_INLINE void
add_tail(list *l, node *n)
{
  node *z = l->tail;

  n->next = (node *) &l->tail;
  n->prev = z;
  z->next = n;
  l->tail = n;
}

LIST_INLINE void
add_head(list *l, node *n)
{
  node *z = l->head;

  n->next = z;
  n->prev = (node *) &l->head;
  z->prev = n;
  l->head = n;
}

LIST_INLINE void
insert_node(node *n, node *after)
{
  node *z = after->next;

  n->next = z;
  n->prev = after;
  after->next = n;
  z->prev = n;
}

LIST_INLINE void
rem_node(node *n)
{
  node *z = n->prev;
  node *x = n->next;

  z->next = x;
  x->prev = z;
}

LIST_INLINE void
init_list(list *l)
{
  l->head = (node *) &l->null;
  l->null = NULL;
  l->tail = (node *) &l->head;
}

LIST_INLINE void
add_tail_list(list *to, list *l)
{
  node *p = to->tail;
  node *q = l->head;

  p->next = q;
  q->prev = p;
  q = l->tail;
  q->next = (node *) &to->null;
  to->tail = q;
}
