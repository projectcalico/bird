/*
 *	BIRD Internet Routing Daemon -- Protocols
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_PROTOCOL_H_
#define _BIRD_PROTOCOL_H_

#include <lib/resource.h>

/*
 *	Routing Protocol
 */

struct protocol {
  char *name;
  unsigned type;			/* ??? List values ??? */
  unsigned debug;			/* Default debugging flags */

  void (*init)(struct protocol *);	/* Boot time */
  void (*preconfig)(struct protocol *);	/* Just before configuring */
  void (*postconfig)(struct protocol *); /* After configuring */
};

void protos_init(void);
void protos_preconfig(void);
void protos_postconfig(void);

/*
 *	Known protocols
 */

extern struct protocol proto_static;

/*
 *	Routing Protocol Instance
 */

struct proto {
  struct proto *next;
  struct protocol *proto;		/* Protocol */
  char *name;				/* Name of this instance */
  unsigned debug;			/* Debugging flags */
  pool *pool;				/* Local objects */
  unsigned preference;			/* Default route preference */

  void (*if_notify)(struct proto *, struct iface *old, struct iface *new);
  void (*rt_notify)(struct proto *, struct rte *old, struct rte *new);
  void (*debug)(struct proto *);		/* Debugging dump */
  void (*start)(struct proto *);		/* Start the instance */
  void (*shutdown)(struct proto *, int time);	/* Stop the instance */

  /* Reconfigure function? */
  /* Interface patterns */
  /* Input/output filters */
  /* Connection to routing tables? */

  /* Hic sunt protocol-specific data */
};

void *proto_new(struct protocol *, unsigned size);

#endif
