/*
 *      BIRD -- OSPF
 *
 *      (c) 1999 Ondrej Filip <feela@network.cz>
 *
 *      Can be freely distributed and used under the terms of the GNU GPL.
 *
 */

#ifndef _BIRD_OSPF_PACKET_H_
#define _BIRD_OSPF_PACKET_H_

void fill_ospf_pkt_hdr(struct ospf_iface *ifa, void *buf, u8 h_type);
void ospf_tx_authenticate(struct ospf_iface *ifa, struct ospf_packet *pkt);
void ospf_pkt_finalize(struct ospf_iface *ifa, struct ospf_packet *pkt);
int  ospf_rx_hook(sock *sk, int size);
void ospf_tx_hook(sock *sk);
void ospf_err_hook(sock *sk, int err);

#endif /* _BIRD_OSPF_PACKET_H_ */
