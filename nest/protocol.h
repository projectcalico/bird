/*
 *	BIRD Internet Routing Daemon -- Protocols
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_PROTOCOL_H_
#define _BIRD_PROTOCOL_H_

#include "lib/lists.h"
#include "lib/resource.h"

struct iface;
struct rte;
struct neighbor;
struct rtattr;
struct network;

/*
 *	Routing Protocol
 */

struct protocol {
  node n;
  char *name;
  unsigned debug;			/* Default debugging flags */

  void (*init)(struct protocol *);	/* Boot time */
  void (*preconfig)(struct protocol *);	/* Just before configuring */
  void (*postconfig)(struct protocol *); /* After configuring */
};

void protos_init(void);
void protos_preconfig(void);
void protos_postconfig(void);
void protos_start(void);
void protos_dump_all(void);

extern list protocol_list;

/*
 *	Known protocols
 */

extern struct protocol proto_device;
extern struct protocol proto_rip;

/*
 *	Routing Protocol Instance
 */

struct proto {
  node n;
  struct protocol *proto;		/* Protocol */
  char *name;				/* Name of this instance */
  unsigned debug;			/* Debugging flags */
  pool *pool;				/* Local objects */
  unsigned preference;			/* Default route preference */
  int ready;				/* Already initialized */

  void (*if_notify)(struct proto *, unsigned flags, struct iface *new, struct iface *old);
  void (*rt_notify)(struct proto *, struct network *net, struct rte *new, struct rte *old);
  void (*neigh_notify)(struct neighbor *neigh);
  void (*dump)(struct proto *);			/* Debugging dump */
  void (*start)(struct proto *);		/* Start the instance */
  void (*shutdown)(struct proto *, int time);	/* Stop the instance */

  int (*rta_same)(struct rtattr *, struct rtattr *);
  int (*rte_better)(struct rte *, struct rte *);

  /* Reconfigure function? */
  /* Interface patterns */
  /* Input/output filters */
  /* Connection to routing tables? */

  /* Hic sunt protocol-specific data */
};

void *proto_new(struct protocol *, unsigned size);

extern list proto_list, inactive_proto_list;

#endif
