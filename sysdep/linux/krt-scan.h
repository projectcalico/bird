/*
 *	BIRD -- Linux Kernel Route Syncer -- Scanning Parameters
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_KRT_SCAN_H_
#define _BIRD_KRT_SCAN_H_

struct krt_scan_params {
  int recurrence;			/* How often should we scan krt, 0=only on startup */
  struct timer *timer;
};

#endif
