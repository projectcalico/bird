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
struct lsah_n {
  node n;
  struct ospf_lsa_header lsa;
};

void ospf_lsack_direct_tx(struct ospf_neighbor *n,struct ospf_lsa_header *h);
void ospf_lsack_rx(struct ospf_lsack_packet *ps, struct proto *p,
  struct ospf_iface *ifa, u16 size);
void ospf_lsack_delay_tx(struct ospf_neighbor *n);
void ospf_lsa_delay(struct ospf_neighbor *n,struct ospf_lsa_header *h,
  struct proto *p);
#endif /* _BIRD_OSPF_LSACK_H_ */
