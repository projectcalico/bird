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

void ospf_lsupd_tx_list(struct ospf_neighbor *n, list *l);
void ospf_lsupd_receive(struct ospf_lsupd_packet *ps,
  struct ospf_iface *ifa, u16 size);
int flood_lsa(struct ospf_neighbor *n, struct ospf_lsa_header *hn,
  struct ospf_lsa_header *hh, struct proto_ospf *po, struct ospf_iface *iff,
  struct ospf_area *oa, int rtl);
void net_flush_lsa(struct top_hash_entry *en, struct proto_ospf *po,
		  struct ospf_area *oa);


#endif /* _BIRD_OSPF_LSUPD_H_ */
