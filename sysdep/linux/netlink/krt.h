/*
 *	BIRD -- Linux Netlink Interface
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_NETLINK_KRT_H_
#define _BIRD_NETLINK_KRT_H_

extern struct protocol proto_unix_kernel;

struct krt_config {
  struct proto_config c;
  int persist;			/* Keep routes when we exit */
  int scan_time;		/* How often we re-scan interfaces */
  int route_scan_time;		/* How often we re-scan routes */
  int learn;			/* Learn routes from other sources */
};

extern struct proto_config *cf_krt;

struct krt_proto {
  struct proto p;
};

void scan_if_init(void);

#endif
