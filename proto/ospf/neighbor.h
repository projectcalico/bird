/*
 *      BIRD -- OSPF
 *
 *      (c) 1999 Ondrej Filip <feela@network.cz>
 *
 *      Can be freely distributed and used under the terms of the GNU GPL.
 *
 */

#ifndef _BIRD_OSPF_NEIGHBOR_H_
#define _BIRD_OSPF_NEIGHBOR_H_

void neigh_chstate(struct ospf_neighbor *n, u8 state);
struct ospf_neighbor *electbdr(list nl);
struct ospf_neighbor *electdr(list nl);
int can_do_adj(struct ospf_neighbor *n);
void tryadj(struct ospf_neighbor *n, struct proto *p);
void ospf_neigh_sm(struct ospf_neighbor *n, int event);
void bdr_election(struct ospf_iface *ifa, struct proto *p);
struct ospf_neighbor *find_neigh(struct ospf_iface *ifa, u32 rid);
struct ospf_area *ospf_find_area(struct proto_ospf *po, u32 aid);

#endif /* _BIRD_OSPF_NEIGHBOR_H_ */
