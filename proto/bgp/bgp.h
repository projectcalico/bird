/*
 *	BIRD -- The Border Gateway Protocol
 *
 *	(c) 2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_BGP_H_
#define _BIRD_BGP_H_

struct bgp_config {
  struct proto_config c;
  unsigned int local_as, remote_as;
  ip_addr remote_ip;
  int multihop;				/* Number of hops if multihop */
};

struct bgp_proto {
  struct proto p;
};

struct bgp_route {
};

struct bgp_attrs {
};

void bgp_check(struct bgp_config *c);

#endif
