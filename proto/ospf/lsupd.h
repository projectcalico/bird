/*
 *      BIRD -- OSPF
 *
 *      (c) 2000 Ondrej Filip <feela@network.cz>
 *
 *      Can be freely distributed and used under the terms of the GNU GPL.
 *
 */

#ifndef _BIRD_OSPF_LSUPD_H_
#define _BIRD_OSPF_LSUPD_H_

void ospf_lsupd_tx(struct ospf_neighbor *n);
void ospf_lsupd_tx_list(struct ospf_neighbor *n, list *l);
void ospf_lsupd_rx(struct ospf_lsupd_packet *ps, struct proto *p,
  struct ospf_iface *ifa, u16 size);

#endif /* _BIRD_OSPF_LSUPD_H_ */
