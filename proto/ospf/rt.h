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

struct infib {
  struct fib_node fn;
  u16 metric;
  u16 pad;
  struct top_hash_entry *en;
};

void ospf_rt_spfa(struct ospf_area *oa);
void add_cand(list *l, struct top_hash_entry *en, struct top_hash_entry *par,
  u16 dist, struct ospf_area *oa);
void calc_next_hop(struct top_hash_entry *par, struct top_hash_entry *en,
  struct ospf_area *oa);
void init_infib(struct fib_node *fn);

#endif /* _BIRD_OSPF_RT_H_ */
