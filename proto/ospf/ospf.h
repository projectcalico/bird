/*
 *	BIRD -- OSPF
 *
 *	(c) 1999 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_OSPF_H_
#define _BIRD_OSPF_H_

struct ospf_config {
  struct proto_config c;
  ip_addr area;		/* Area ID */
};

#endif /* _BIRD_OSPF_H_ */
