/*
 *	BIRD -- Static Route Generator
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_STATIC_H_
#define _BIRD_STATIC_H_

struct static_proto {
  struct proto p;
  list routes;
};

void static_init_instance(struct static_proto *);

struct static_route {
  node n;
  u32 net;				/* Network we route */
  int masklen;				/* Mask length */
  int dest;				/* Destination type (RTD_*) */
  u32 via;				/* Destination router */
  struct neighbor *neigh;
  /* FIXME: Device routes, maybe via device patterns? */
  /* FIXME: More route attributes, probably via filter syntax */
};

#endif
