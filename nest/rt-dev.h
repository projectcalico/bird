/*
 *	BIRD -- Direct Device Routes
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_RT_DEV_H_
#define _BIRD_RT_DEV_H_

struct rt_dev_proto {
  struct proto p;
  list iface_list;
};

#endif
