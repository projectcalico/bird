/*
 *	BIRD Internet Routing Daemon -- Network Interfaces
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_IFACE_H_
#define _BIRD_IFACE_H_

#include "lib/lists.h"

extern list iface_list;

struct proto;

struct iface {
  node n;
  char name[16];
  unsigned flags;
  unsigned mtu;
  unsigned index;			/* OS-dependent interface index */
  ip_addr ip;				/* IP address of this host */
  ip_addr prefix;			/* Network prefix */
  unsigned pxlen;			/* Prefix length */
  ip_addr brd;				/* Broadcast address */
  ip_addr opposite;			/* Opposite end of a point-to-point link */
  struct neighbor *neigh;		/* List of neighbors on this interface */
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

/* Interface change events */

#define IF_CHANGE_UP 1
#define IF_CHANGE_DOWN 2
#define IF_CHANGE_FLAGS 4		/* Can be converted to down/up internally */
#define IF_CHANGE_MTU 8
#define IF_CHANGE_CREATE 16		/* Seen this interface for the first time */

void if_init(void);
void if_dump(struct iface *);
void if_dump_all(void);
void if_update(struct iface *);
void if_end_update(void);
void if_feed_baby(struct proto *);

/*
 *	Neighbor Cache. We hold (direct neighbor, protocol) pairs we've seen
 *	along with pointer to protocol-specific data.
 *
 *	The primary goal of this cache is to quickly validate all incoming
 *	packets if their have been sent by our neighbors and to notify
 *	protocols about lost neighbors when an interface goes down.
 *
 *	Anyway, it can also contain `sticky' entries for currently unreachable
 *	addresses which cause notification when the address becomes a neighbor.
 */

typedef struct neighbor {
  node n;				/* Node in global neighbor list */
  ip_addr addr;				/* Address of the neighbor */
  struct iface *iface;			/* Interface it's connected to */
  struct neighbor *sibling;		/* Next in per-device chain */
  struct proto *proto;			/* Protocol this belongs to */
  void *data;				/* Protocol-specific data */
  unsigned flags;
} neighbor;

#define NEF_STICKY 1

/*
 * Find neighbor or return NULL if it doesn't exist.
 * If you specify flags == NEF_STICKY, a sticky entry is created if the
 * address is not a neighbor, but NULL can still be returned if the address
 * given is invalid.
 */
neighbor *neigh_find(struct proto *, ip_addr *, unsigned flags);

void neigh_dump(neighbor *);
void neigh_dump_all(void);
void neigh_prune(void);

/*
 *	Interface Pattern Lists
 */

struct iface_patt {
  node n;
  byte *pattern;			/* Interface name pattern */

  /* Protocol-specific data follow, but keep them like this:
     struct rip_iface_patt {
        struct iface_patt i;
	whatever you (need);
     }
   */
};

struct iface_patt *iface_patt_match(list *, struct iface *);
int iface_patts_equal(list *, list *, int (*)(struct iface_patt *, struct iface_patt *));

#endif
