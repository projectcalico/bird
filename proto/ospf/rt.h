/*
 *      BIRD -- OSPF
 *
 *      (c) 2000--2004 Ondrej Filip <feela@network.cz>
 *
 *      Can be freely distributed and used under the terms of the GNU GPL.
 *
 */

#ifndef _BIRD_OSPF_RT_H_
#define _BIRD_OSPF_RT_H_

struct infib
{
  struct fib_node fn;
  u16 metric;
  u16 oldmetric;
  struct top_hash_entry *en;
  struct top_hash_entry *olden;
};

struct extfib
{
  struct fib_node fn;
  u16 metric;
  u16 metric2;
  ip_addr nh;
  u32 tag;
  struct iface *nhi;
  u16 oldmetric;
  u16 oldmetric2;
  ip_addr oldnh;
  u32 oldtag;
};

void ospf_rt_spf(struct proto_ospf *po);
void init_infib(struct fib_node *fn);
void init_efib(struct fib_node *fn);

#endif /* _BIRD_OSPF_RT_H_ */
