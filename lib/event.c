/*
 *	BIRD Library -- Event Processing
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "nest/bird.h"
#include "lib/event.h"

event_list global_event_list;

inline void
ev_postpone(event *e)
{
  if (e->n.next)
    {
      rem_node(&e->n);
      e->n.next = NULL;
    }
}

static void
ev_dump(resource *r)
{
  event *e = (event *) r;

  debug("(code %p, data %p, %s)\n",
	e->hook,
	e->data,
	e->n.next ? "scheduled" : "inactive");
}

static struct resclass ev_class = {
  "Event",
  sizeof(event),
  (void (*)(resource *)) ev_postpone,
  ev_dump
};

event *
ev_new(pool *p)
{
  event *e = ralloc(p, &ev_class);

  e->hook = NULL;
  e->data = NULL;
  e->n.next = NULL;
  return e;
}

inline int
ev_run(event *e)
{
  int keep = e->hook(e->data);
  if (!keep)
    ev_postpone(e);
  return keep;
}

inline void
ev_enqueue(event_list *l, event *e)
{
  if (e->n.next)
    rem_node(&e->n);
  add_tail(l, &e->n);
}

void
ev_schedule(event *e)
{
  ev_enqueue(&global_event_list, e);
}

int
ev_run_list(event_list *l)
{
  node *n, *p;
  int keep = 0;

  WALK_LIST_DELSAFE(n, p, *l)
    {
      event *e = SKIP_BACK(event, n, n);
      keep += ev_run(e);
    }
  return keep;
}
