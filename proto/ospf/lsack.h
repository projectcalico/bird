/*
 *      BIRD -- OSPF
 *
 *      (c) 2000 Ondrej Filip <feela@network.cz>
 *
 *      Can be freely distributed and used under the terms of the GNU GPL.
 *
 */

#ifndef _BIRD_OSPF_LSACK_H_
#define _BIRD_OSPF_LSACK_H_

void ospf_lsack_tx(struct ospf_neighbor *n);
void ospf_lsack_rx(struct ospf_lsack_packet *ps, struct proto *p,
  struct ospf_iface *ifa, u16 size);

#endif /* _BIRD_OSPF_LSACK_H_ */
