/*
 *      BIRD -- OSPF
 *
 *      (c) 1999 - 2000 Ondrej Filip <feela@network.cz>
 *
 *      Can be freely distributed and used under the terms of the GNU GPL.
 *
 */

#ifndef _BIRD_OSPF_HELLO_H_
#define _BIRD_OSPF_HELLO_H_

void install_inactim(struct ospf_neighbor *n);
void restart_inactim(struct ospf_neighbor *n);
void restart_hellotim(struct ospf_iface *ifa);
void restart_polltim(struct ospf_iface *ifa);
void restart_waittim(struct ospf_iface *ifa);
void ospf_hello_rx(struct ospf_hello_packet *ps, struct proto *p,
  struct ospf_iface *ifa, int size, ip_addr faddr);
void hello_timer_hook(timer *timer);
void poll_timer_hook(timer *timer);
void wait_timer_hook(timer *timer);
void hello_send(timer *timer,int poll, struct ospf_neighbor *dirn);

#endif /* _BIRD_OSPF_HELLO_H_ */
