/*
 *	BIRD Internet Routing Daemon -- Network Interfaces
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_IFACE_H_
#define _BIRD_IFACE_H_

#include <lib/lists.h>

struct iface {
  node n;
  char *name;
  unsigned flags;
  struct ifa *ifa;			/* First address is primary */
};

#define IF_UP 1
#define IF_MULTIACCESS 2
#define IF_UNNUMBERED 4
#define IF_BROADCAST 8
#define IF_MULTICAST 16
#define IF_TUNNEL 32

/* Interface address */

struct ifa {
  struct ifa *next;
  ip_addr ip;				/* IP address of this host */
  ip_addr prefix;			/* Network prefix */
  unsigned pxlen;			/* Prefix length */
  ip_addr brd;				/* Broadcast address */
};

#endif
