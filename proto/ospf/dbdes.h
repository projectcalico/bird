/*
 *      BIRD -- OSPF
 *
 *      (c) 1999 Ondrej Filip <feela@network.cz>
 *
 *      Can be freely distributed and used under the terms of the GNU GPL.
 *
 */

#ifndef _BIRD_OSPF_DBDES_H_
#define _BIRD_OSPF_DBDES_H_

void ospf_dbdes_tx(struct ospf_neighbor *n);
void rxmt_timer_hook(timer *timer);
void ospf_dbdes_rx(struct ospf_dbdes_packet *ps, struct proto *p,
  struct ospf_iface *ifa, u16 size);

#endif /* _BIRD_OSPF_DBDES_H_ */
