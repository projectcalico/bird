/*
 *      BIRD -- OSPF
 *
 *      (c) 2000 Ondrej Filip <feela@network.cz>
 *
 *      Can be freely distributed and used under the terms of the GNU GPL.
 *
 */

#ifndef _BIRD_OSPF_RT_H_
#define _BIRD_OSPF_RT_H_

struct stub_fib {
  struct fib_node fn;
  u16 metric;
  u16 pad;
  ip_addr nh;
  struct iface *nhi;
};

void ospf_rt_spfa(struct ospf_area *oa, struct proto *p);
void add_cand(list *l, struct top_hash_entry *en, struct top_hash_entry *par,
  u16 dist, struct proto *p, struct ospf_area *oa);
void calc_next_hop(struct top_hash_entry *par, struct top_hash_entry *en,
  struct proto *p, struct ospf_area *oa);
void calc_next_hop_fib(struct top_hash_entry *par, struct stub_fib *en,
  struct proto *p, struct ospf_area *oa);

#endif /* _BIRD_OSPF_RT_H_ */
