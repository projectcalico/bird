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

struct ifa {				/* Interface address */
  node n;
  struct iface *iface;			/* Interface this address belongs to */
  ip_addr ip;				/* IP address of this host */
  ip_addr prefix;			/* Network prefix */
  unsigned pxlen;			/* Prefix length */
  ip_addr brd;				/* Broadcast address */
  ip_addr opposite;			/* Opposite end of a point-to-point link */
  unsigned scope;			/* Interface address scope */
  unsigned flags;			/* Analogous to iface->flags */
};

struct iface {
  node n;
  char name[16];
  unsigned flags;
  unsigned mtu;
  unsigned index;			/* OS-dependent interface index */
  list addrs;				/* Addresses assigned to this interface */
  struct ifa *addr;			/* Primary address */
  list neighbors;			/* All neighbors on this interface */
};

#define IF_UP 1				/* IF_LINK_UP and IP address known */
#define IF_MULTIACCESS 2
#define IF_UNNUMBERED 4
#define IF_BROADCAST 8
#define IF_MULTICAST 0x10
#define IF_ADMIN_DOWN 0x40
#define IF_LOOPBACK 0x80
#define IF_IGNORE 0x100			/* Not to be used by routing protocols (loopbacks etc.) */
#define IF_LINK_UP 0x200

#define IA_PRIMARY 0x10000		/* This address is primary */
#define IA_SECONDARY 0x20000		/* This address has been reported as secondary by the kernel */
#define IA_FLAGS 0xff0000

#define IF_JUST_CREATED 0x10000000	/* Send creation event as soon as possible */
#define IF_TMP_DOWN 0x20000000		/* Temporary shutdown due to interface reconfiguration */
#define IF_UPDATED 0x40000000		/* Touched in last scan */

/* Interface change events */

#define IF_CHANGE_UP 1
#define IF_CHANGE_DOWN 2
#define IF_CHANGE_MTU 4
#define IF_CHANGE_CREATE 8		/* Seen this interface for the first time */
#define IF_CHANGE_TOO_MUCH 0x40000000	/* Used internally */

void if_init(void);
void if_dump(struct iface *);
void if_dump_all(void);
void ifa_dump(struct ifa *);
void if_show(void);
void if_show_summary(void);
struct iface *if_update(struct iface *);
struct ifa *ifa_update(struct ifa *);
void ifa_delete(struct ifa *);
void if_start_update(void);
void if_end_update(void);
void if_end_partial_update(struct iface *);
void if_feed_baby(struct proto *);
struct iface *if_find_by_index(unsigned);
struct iface *if_find_by_name(char *);

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
  node if_n;				/* Node in per-interface neighbor list */
  ip_addr addr;				/* Address of the neighbor */
  struct iface *iface;			/* Interface it's connected to */
  struct proto *proto;			/* Protocol this belongs to */
  void *data;				/* Protocol-specific data */
  unsigned aux;				/* Protocol-specific data */
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
  ip_addr prefix;			/* Interface prefix */
  int pxlen;

  /* Protocol-specific data follow after this structure */
};

struct iface_patt *iface_patt_match(list *, struct iface *);
int iface_patts_equal(list *, list *, int (*)(struct iface_patt *, struct iface_patt *));

#endif
