/*
 *	BIRD Internet Routing Daemon -- Protocols
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_PROTOCOL_H_
#define _BIRD_PROTOCOL_H_

#include "lib/lists.h"
#include "lib/resource.h"
#include "lib/timer.h"

struct iface;
struct ifa;
struct rte;
struct neighbor;
struct rta;
struct network;
struct proto_config;
struct config;
struct proto;
struct event;
struct ea_list;
struct symbol;

/*
 *	Routing Protocol
 */

struct protocol {
  node n;
  char *name;
  unsigned debug;			/* Default debugging flags */
  int priority;				/* Protocol priority (usually 0) */
  int name_counter;			/* Counter for automatic name generation */
  int startup_counter;			/* Number of instances waiting for initialization */

  void (*preconfig)(struct protocol *, struct config *);	/* Just before configuring */
  void (*postconfig)(struct proto_config *);			/* After configuring each instance */
  struct proto * (*init)(struct proto_config *);		/* Create new instance */
  int (*reconfigure)(struct proto *, struct proto_config *);	/* Try to reconfigure instance */
  void (*dump)(struct proto *);			/* Debugging dump */
  void (*dump_attrs)(struct rte *);		/* Dump protocol-dependent attributes */
  int (*start)(struct proto *);			/* Start the instance */
  int (*shutdown)(struct proto *);		/* Stop the instance */
  void (*get_status)(struct proto *, byte *buf); /* Get instance status (for `show protocols' command) */
  void (*get_route_info)(struct rte *, byte *buf); /* Get route information (for `show route' command) */
  void (*show_route_data)(struct rte *);	/* Print verbose route information (`show route' again) */
};

void protos_build(void);
void protos_preconfig(struct config *);
void protos_postconfig(struct config *);
void protos_commit(struct config *);
void protos_start(void);
void protos_dump_all(void);
void protos_shutdown(void);

extern list protocol_list;

/*
 *	Known protocols
 */

extern struct protocol proto_device;
extern struct protocol proto_rip;
extern struct protocol proto_static;
extern struct protocol proto_ospf;
extern struct protocol proto_pipe;

/*
 *	Routing Protocol Instance
 */

struct proto_config {
  node n;
  struct config *global;		/* Global configuration data */
  struct protocol *protocol;		/* Protocol */
  struct proto *proto;			/* Instance we've created */
  char *name;
  unsigned debug, preference, disabled;	/* Generic parameters */
  struct rtable_config *table;		/* Table we're attached to */
  struct filter *in_filter, *out_filter; /* Attached filters */

  /* Protocol-specific data follow... */
};

struct proto {
  node n;
  struct protocol *proto;		/* Protocol */
  struct proto_config *cf;		/* Configuration data */
  pool *pool;				/* Pool containing local objects */
  struct event *attn;			/* "Pay attention" event */

  char *name;				/* Name of this instance (== cf->name) */
  unsigned debug;			/* Debugging flags */
  unsigned preference;			/* Default route preference */
  unsigned disabled;			/* Manually disabled */
  unsigned proto_state;			/* Protocol state machine (see below) */
  unsigned core_state;			/* Core state machine (see below) */
  unsigned core_goal;			/* State we want to reach (see below) */
  bird_clock_t last_state_change;	/* Time of last state transition */

  /*
   *	General protocol hooks:
   *
   *	   if_notify	Notify protocol about interface state changes.
   *	   ifa_notify	Notify protocol about interface address changes.
   *	   rt_notify	Notify protocol about routing table updates.
   *	   neigh_notify	Notify protocol about neighbor cache events.
   *	   make_tmp_attrs  Construct ea_list from private attrs stored in rte.
   *	   store_tmp_attrs Store private attrs back to the rte.
   *	   import_control  Called as the first step of the route importing process.
   *			It can construct a new rte, add private attributes and
   *			decide whether the route shall be imported: 1=yes, -1=no,
   *			0=process it through the import filter set by the user.
   */

  void (*if_notify)(struct proto *, unsigned flags, struct iface *i);
  void (*ifa_notify)(struct proto *, unsigned flags, struct ifa *a);
  void (*rt_notify)(struct proto *, struct network *net, struct rte *new, struct rte *old, struct ea_list *tmpa);
  void (*neigh_notify)(struct neighbor *neigh);
  struct ea_list *(*make_tmp_attrs)(struct rte *rt, struct linpool *pool);
  void (*store_tmp_attrs)(struct rte *rt, struct ea_list *attrs);
  int (*import_control)(struct proto *, struct rte **rt, struct ea_list **attrs, struct linpool *pool);

  /*
   *	Routing entry hooks (called only for rte's belonging to this protocol):
   *
   *	   rte_better	Compare two rte's and decide which one is better (1=first, 0=second).
   *	   rte_insert	Called whenever a rte is inserted to a routing table.
   *	   rte_remove	Called whenever a rte is removed from the routing table.
   */

  int (*rte_better)(struct rte *, struct rte *);
  void (*rte_insert)(struct network *, struct rte *);
  void (*rte_remove)(struct network *, struct rte *);

  struct rtable *table;			/* Our primary routing table */
  struct filter *in_filter;		/* Input filter */
  struct filter *out_filter;		/* Output filter */
  struct announce_hook *ahooks;		/* Announcement hooks for this protocol */

  /* Hic sunt protocol-specific data */
};

void proto_build(struct proto_config *);
void *proto_new(struct proto_config *, unsigned size);
void *proto_config_new(struct protocol *, unsigned size);
void proto_show(struct symbol *, int);

extern list proto_list;

/*
 *  Each protocol instance runs two different state machines:
 *
 *  [P] The protocol machine: (implemented inside protocol)
 *
 *		DOWN    ---->    START
 *		  ^		   |
 *		  |		   V
 *		STOP    <----     UP
 *
 *	States:	DOWN	Protocol is down and it's waiting for the core
 *			requesting protocol start.
 *		START	Protocol is waiting for connection with the rest
 *			of the network and it's not willing to accept
 *			packets. When it connects, it goes to UP state.
 *		UP	Protocol is up and running. When the network
 *			connection breaks down or the core requests
 *			protocol to be terminated, it goes to STOP state.
 *		STOP	Protocol is disconnecting from the network.
 *			After it disconnects, it returns to DOWN state.
 *
 *	In:	start()	Called in DOWN state to request protocol startup.
 *			Returns new state: either UP or START (in this
 *			case, the protocol will notify the core when it
 *			finally comes UP).
 *		stop()	Called in START, UP or STOP state to request
 *			protocol shutdown. Returns new state: either
 *			DOWN or STOP (in this case, the protocol will
 *			notify the core when it finally comes DOWN).
 *
 *	Out:	proto_notify_state() -- called by protocol instance when
 *			it does any state transition not covered by
 *			return values of start() and stop(). This includes
 *			START->UP (delayed protocol startup), UP->STOP
 *			(spontaneous shutdown) and STOP->DOWN (delayed
 *			shutdown).
 */

#define PS_DOWN 0
#define PS_START 1
#define PS_UP 2
#define PS_STOP 3

void proto_notify_state(struct proto *p, unsigned state);

/*
 *  [F] The feeder machine: (implemented in core routines)
 *
 *		HUNGRY    ---->   FEEDING
 *		 ^		     |
 *		 | 		     V
 *		FLUSHING  <----   HAPPY
 *
 *	States:	HUNGRY	Protocol either administratively down (i.e.,
 *			disabled by the user) or temporarily down
 *			(i.e., [P] is not UP)
 *		FEEDING	The protocol came up and we're feeding it
 *			initial routes. [P] is UP.
 *		HAPPY	The protocol is up and it's receiving normal
 *			routing updates. [P] is UP.
 *		FLUSHING The protocol is down and we're removing its
 *			routes from the table. [P] is STOP or DOWN.
 *
 *	Normal lifecycle of a protocol looks like:
 *
 *		HUNGRY/DOWN --> HUNGRY/START --> HUNGRY/UP -->
 *		FEEDING/UP --> HAPPY/UP --> FLUSHING/STOP|DOWN -->
 *		HUNGRY/STOP|DOWN --> HUNGRY/DOWN
 */

#define FS_HUNGRY 0
#define FS_FEEDING 1
#define FS_HAPPY 2
#define FS_FLUSHING 3

/*
 *	Known unique protocol instances as referenced by config routines
 */

extern struct proto_config *cf_dev_proto;

/*
 *	Route Announcement Hook
 */

struct announce_hook {
  node n;
  struct rtable *table;
  struct proto *proto;
  struct announce_hook *next;		/* Next hook for the same protocol */
};

struct announce_hook *proto_add_announce_hook(struct proto *, struct rtable *);

/*
 *	Callback to sysdep code when shutdown is finished
 */

void protos_shutdown_notify(void);

#endif
