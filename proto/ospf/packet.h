/*
 *      BIRD -- OSPF
 *
 *      (c) 1999 - 2000 Ondrej Filip <feela@network.cz>
 *
 *      Can be freely distributed and used under the terms of the GNU GPL.
 *
 */

#ifndef _BIRD_OSPF_PACKET_H_
#define _BIRD_OSPF_PACKET_H_

void fill_ospf_pkt_hdr(struct ospf_iface *ifa, void *buf, u8 h_type);
void ospf_tx_authenticate(struct ospf_iface *ifa, struct ospf_packet *pkt);
void ospf_pkt_finalize(struct ospf_iface *ifa, struct ospf_packet *pkt);
int ospf_rx_hook(sock * sk, int size);
void ospf_tx_hook(sock * sk);
void ospf_err_hook(sock * sk, int err);
void ospf_send_to_agt(sock * sk, u16 len, struct ospf_iface *ifa, u8 state);
void ospf_send_to_bdr(sock * sk, u16 len, struct ospf_iface *ifa);
void ospf_send_to(sock *sk, u16 len, ip_addr ip);


#endif /* _BIRD_OSPF_PACKET_H_ */
