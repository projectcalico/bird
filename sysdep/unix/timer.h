/*
 *	BIRD -- Unix Timers
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_TIMER_H_
#define _BIRD_TIMER_H_

#include <sys/time.h>

#include "lib/resource.h"

typedef time_t bird_clock_t;		/* Use instead of time_t */

typedef struct timer {
  resource r;
  void (*hook)(struct timer *);
  void *data;
  unsigned randomize;			/* Amount of randomization */
  unsigned recurrent;			/* Timer recurrence */
  node n;				/* Internal link */
  clock_t expires;			/* 0=inactive */
} timer;

timer *tm_new(pool *);
void tm_start(timer *, unsigned after);
void tm_stop(timer *);
void tm_dump_all(void);

extern clock_t now;

#endif
