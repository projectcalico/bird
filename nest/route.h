/*
 *	BIRD Internet Routing Daemon -- Routing Table
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_ROUTE_H_
#define _BIRD_ROUTE_H_

#include "lib/resource.h"
#include "lib/timer.h"

struct protocol;
struct proto;

/*
 *	Generic data structure for storing network prefixes. Also used
 *	for the master routing table. Currently implemented as a hash 
 *	table.
 *
 *	Available operations:
 *		- insertion of new entry
 *		- deletion of entry
 *		- searching for entry by network prefix
 */

struct fib_node {
  ip_addr prefix;			/* In host order */
  byte pxlen;
  byte flags;				/* ??? define them ??? */
  byte pad0, pad1;			/* ??? use ??? */
  struct fib_node *next;		/* Next in hash chain */
};

struct fib {
  pool *fib_pool;			/* Pool holding all our data */
  slab *fib_slab;			/* Slab holding all fib nodes */
  struct fib_node **hash_table;		/* Node hash table */
  unsigned int hash_size;		/* Number of hash table entries (a power of two) */
  unsigned int hash_mask;		/* hash_size - 1 */
  unsigned int entries;			/* Number of entries */
  unsigned int entries_min, entries_max;/* Entry count limits (else start rehashing) */
  void (*init)(struct fib_node *);	/* Constructor */
};

void fib_init(struct fib *, pool *, unsigned node_size, unsigned hash_size, void (*init)(struct fib_node *));
void *fib_find(struct fib *, ip_addr *, int);	/* Find or return NULL if doesn't exist */
void *fib_get(struct fib *, ip_addr *, int); 	/* Find or create new if nonexistent */
void fib_delete(struct fib *, void *);	/* Remove fib entry */
void fib_free(struct fib *);		/* Destroy the fib */

#define FIB_WALK(fib, z) do {					\
	struct fib_node *z, **ff = (fib)->hash_table;		\
	unsigned int count = (fib)->hash_size;			\
	while (count--)						\
	  for(z = *ff++; z; z=z->next)

#define FIB_WALK_END } while (0)

/*
 *	Master Routing Tables. Generally speaking, each of them is a list
 *	of FIB (one per TOS) with each entry pointing to a list of route entries
 *	representing routes to given network.
 *	Each of the RTE's contains variable data (the preference and protocol-dependent
 *	metrics) and a pointer to a route attribute block common for many routes).
 */

typedef struct rtable {
  struct rtable *sibling;		/* Our sibling for different TOS */
  byte tos;				/* TOS for this table */
  struct fib fib;
  char *name;				/* Name of this table */
  /* FIXME: Data for kernel synchronization */
} rtable;

typedef struct network {
  struct fib_node n;
  struct rte *routes;			/* Available routes for this network */
  struct network *next;			/* Next in Recalc Chain */
} net;

typedef struct rte {
  struct rte *next;
  struct rtattr *attrs;
  byte flags;				/* Flags (REF_...) */
  byte pflags;				/* Protocol-specific flags */
  word pref;				/* Route preference */
  bird_clock_t lastmod;			/* Last modified */
  union {				/* Protocol-dependent data (metrics etc.) */
#ifdef CONFIG_STATIC
    struct {
    } stat;
#endif
#ifdef CONFIG_RIP
    struct {
      byte metric;			/* RIP metric */
      u16 tag;				/* External route tag */
    } rip;
#endif
#ifdef CONFIG_OSPF
    struct {
      u32 metric1, metric2;		/* OSPF Type 1 and Type 2 metrics */
      u32 tag;				/* External route tag */
    } ospf;
#endif
#ifdef CONFIG_BGP
    struct {
    } bgp;
#endif
  } u;
} rte;

#define REF_CHOSEN 1			/* Currently chosen route */

extern rtable master_table;

void rt_init(void);
void rt_setup(rtable *, char *);
net *net_find(rtable *tab, unsigned tos, ip_addr addr, unsigned len);
net *net_get(rtable *tab, unsigned tos, ip_addr addr, unsigned len);
rte *rte_find(net *net, struct proto *p);
rte *rte_get_temp(struct rtattr *);
void rte_update(net *net, struct proto *p, rte *new);
void rte_discard(net *net, rte *old);
void rte_dump(net *, rte *);
void rt_dump(rtable *);
void rt_dump_all(void);
void rt_feed_baby(struct proto *p);

/*
 *	Route Attributes
 *
 *	Beware: All standard BGP attributes must be represented here instead
 *	of making them local to the route. This is needed to ensure proper
 *	construction of BGP route attribute lists.
 */

typedef struct rtattr {
  struct rtattr *next, *prev;		/* Hash chain */
  struct rtattr *garbage;		/* Garbage collector chain */
  struct proto *proto;			/* Protocol instance */
  unsigned uc;				/* Use count */
  byte source;				/* Route source (RTS_...) */
  byte scope;				/* Route scope (SCOPE_... -- see ip.h) */
  byte cast;				/* Casting type (RTC_...) */
  byte dest;				/* Route destination type (RTD_...) */
  byte tos;				/* TOS of this route */
  byte flags;				/* Route flags (RTF_...) */
  ip_addr gw;				/* Next hop */
  ip_addr from;				/* Advertising router */
  struct iface *iface;			/* Outgoing interface */
  struct ea_list *attrs;		/* Extended Attribute chain */
  union {				/* Protocol-specific data */
  } u;
} rta;

#define RTS_STATIC 1			/* Normal static route */
#define RTS_INHERIT 2			/* Route inherited from kernel */
#define RTS_DEVICE 3			/* Device route */
#define RTS_STATIC_DEVICE 4		/* Static device route */
#define RTS_REDIRECT 5			/* Learned via redirect */
#define RTS_RIP 6			/* RIP route */
#define RTS_RIP_EXT 7			/* RIP external route */
#define RTS_OSPF 8			/* OSPF route */
#define RTS_OSPF_EXT 9			/* OSPF external route */
#define RTS_OSPF_IA 10			/* OSPF inter-area route */
#define RTS_OSPF_BOUNDARY 11		/* OSPF route to boundary router (???) */
#define RTS_BGP 12			/* BGP route */

#define RTC_UNICAST 0
#define RTC_BROADCAST 1
#define RTC_MULTICAST 2
#define RTC_ANYCAST 3			/* IPv6 Anycast */

#define RTD_ROUTER 0			/* Next hop is neighbor router */
#define RTD_DEVICE 1			/* Points to device */
#define RTD_BLACKHOLE 2			/* Silently drop packets */
#define RTD_UNREACHABLE 3		/* Reject as unreachable */
#define RTD_PROHIBIT 4			/* Administratively prohibited */

#define RTF_EXTERIOR 1			/* Learned via exterior protocol */
#define RTF_TAGGED 2			/* Tagged external route learned via IGP */

/*
 *	Extended Route Attributes
 */

typedef struct eattr {
  byte protocol;			/* Protocol ID (EAP_...) */
  byte flags;				/* Attribute flags (EAF_...) */
  byte id;				/* Protocol-dependent ID */
  byte rfu;				/* ??? */
  union {
    u32 data;
    struct adata *ptr;			/* Attribute data elsewhere */
  } u;
} eattr;

#define EAP_GENERIC 0			/* Generic attributes */
#define EAP_BGP 1			/* BGP attributes */

#define EAF_OPTIONAL 0x80		/* Refer to BGP specs for full meaning */
#define EAF_TRANSITIVE 0x40
#define EAF_PARTIAL 0x20
#define EAF_EXTENDED_LENGTH 0x10	/* Not used by us, internal to BGP */
#define EAF_LONGWORD 0x01		/* Embedded value [Not a BGP flag!] */

struct adata {
  unsigned int length;
  byte data[0];
};

typedef struct ea_list {
  struct ea_list *next;			/* In case we have an override list */
  byte sorted;				/* `Really sorted' flag (???) */
  byte rfu;
  word nattrs;				/* Number of attributes */
  eattr attrs[0];			/* Attribute definitions themselves */
} ea_list;

eattr *ea_find(ea_list *, unsigned protocol, unsigned id);

#define EA_LIST_NEW(p, alloc, n) do {				\
	unsigned cnt = n;				       	\
	p = alloc(sizeof(ea_list) + cnt*sizeof(eattr));		\
	memset(p, 0, sizeof(ea_list));				\
	p->nattrs = cnt;					\
} while(0)

void rta_init(void);
rta *rta_lookup(rta *);			/* Get rta equivalent to this one, uc++ */
static inline rta *rta_clone(rta *r) { r->uc++; return r; }
void _rta_free(rta *r);
static inline void rta_free(rta *r) { if (r && !--r->uc) _rta_free(r); }
void rta_dump(rta *);
void rta_dump_all(void);

/*
 *	Default protocol preferences
 */

#define DEF_PREF_DIRECT	    	240	/* Directly connected */
#define DEF_PREF_STATIC		200	/* Static route */
#define DEF_PREF_OSPF_INTERNAL	150	/* OSPF intra-area, inter-area and type 1 external routes */
#define DEF_PREF_RIP		120	/* RIP */
#define DEF_PREF_BGP		100	/* BGP */
#define DEF_PREF_OSPF_EXTERNAL	80	/* OSPF external routes */
#define DEF_PREF_RIP_EXTERNAL	70	/* RIP external routes */
#define DEF_PREF_SINK		10	/* Sink route */

#endif
