/*
 *	BIRD -- Password handling
 *
 *	Copyright 1999 Pavel Machek <pavel@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "nest/bird.h"
#include "nest/password.h"

struct password_item *last_password_item = NULL;

static int
password_goodness(struct password_item *i)
{
  if (i->from > now)
    return 0;
  if (i->to < now)
    return 0;
  if (i->passive < now)
    return 1;
  return 2;
}

struct password_item *
get_best_password(struct password_item *head, int flags)
{
  int good = -1;
  struct password_item *best = NULL;

  while (head) {
    int cur = password_goodness(head);
    if (cur > good) {
      good = cur;
      best = head;
    }
    head=head->next;
  }
  return best;
}

void
password_strncpy(char *to, char *from, int len)
{
  int i;
  for (i=0; i<len; i++) {
    *to++ = *from;
    if (*from)
      from++;
  }
}

int
password_same(struct password_item *old, struct password_item *new)
{
  for(;;)
    {
      if (old == new)
	return 1;
      if (!old || !new)
	return 0;
      if (old->from    != new->from    ||
	  old->to      != new->to      ||
	  old->passive != new->passive ||
	  old->id      != new->id      ||
	  strcmp(old->password, new->password))
	return 0;
      old = old->next;
      new = new->next;
    }
}
