/*
 *	BIRD Timers
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_TIMER_H_
#define _BIRD_TIMER_H_

#include <lib/resource.h>

typedef struct timer {
	resource r;
	void (*hook)(struct timer *);
	void *data;
	/* internal fields should be here */
} timer;

timer *tm_new(pool *, void (*hook)(timer *), void *data);
void tm_start(timer *, unsigned after);
void tm_stop(timer *);
void tm_trigger(timer *);

#endif
