/*
 *	BIRD Internet Routing Daemon -- Network Interfaces
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_IFACE_H_
#define _BIRD_IFACE_H_

#include "lib/lists.h"

extern list iface_list;

struct iface {
  node n;
  char name[16];
  unsigned flags;
  unsigned mtu;
  struct ifa *ifa;			/* First address is primary */
  unsigned index;			/* OS-dependent interface index */
};

#define IF_UP 1
#define IF_MULTIACCESS 2
#define IF_UNNUMBERED 4
#define IF_BROADCAST 8
#define IF_MULTICAST 16
#define IF_TUNNEL 32
#define IF_ADMIN_DOWN 64
#define IF_LOOPBACK 128
#define IF_IGNORE 256
#define IF_UPDATED 0x1000		/* Touched in last scan */

/* Interface address */

struct ifa {
  struct ifa *next;
  ip_addr ip;				/* IP address of this host */
  ip_addr prefix;			/* Network prefix */
  unsigned pxlen;			/* Prefix length */
  ip_addr brd;				/* Broadcast address */
  ip_addr opposite;			/* Opposite end of a point-to-point link */
  struct neighbor *neigh;		/* List of neighbors on this interface */
};

/* Interface change events */

#define IF_CHANGE_UP 1
#define IF_CHANGE_DOWN 2
#define IF_CHANGE_FLAGS 4
#define IF_CHANGE_MTU 8
#define IF_CHANGE_ADDR 16

void if_init(void);
void if_dump(struct iface *);
void if_dump_all(void);
void if_update(struct iface *);
void if_end_update(void);

#endif
