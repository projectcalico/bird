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

void ospf_rt_spfa(struct ospf_area *oa, struct proto *p);
void add_cand(list *l, struct top_hash_entry *en, struct top_hash_entry *par,
  u16 dist, struct proto *p);
void calc_next_hop(struct top_hash_entry *par, struct top_hash_entry *en,
  struct proto *p);

#endif /* _BIRD_OSPF_RT_H_ */
