/*
 *      BIRD -- OSPF
 *
 *      (c) 2000 Ondrej Filip <feela@network.cz>
 *
 *      Can be freely distributed and used under the terms of the GNU GPL.
 *
 */

#ifndef _BIRD_OSPF_LSREQ_H_
#define _BIRD_OSPF_LSREQ_H_

void ospf_lsreq_tx(struct ospf_neighbor *n);
void lsrr_timer_hook(timer *timer);
void ospf_lsreq_rx(struct ospf_lsreq_packet *ps, struct proto *p,
  struct ospf_iface *ifa, u16 size);

#endif /* _BIRD_OSPF_LSREQ_H_ */
