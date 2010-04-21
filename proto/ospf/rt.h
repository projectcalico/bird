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

#define ORT_UNDEF -1
#define ORT_ROUTER 1
#define ORT_NET 0

typedef struct orta
{
  int type;
  u32 options;
  /*
   * For ORT_ROUTER routes, options field are router-LSA style
   * options, with V,E,B bits. In OSPFv2, ASBRs from another areas
   * (that we know from rt-summary-lsa) have just ORTA_ASBR in
   * options, their real options are unknown.
   */
#define ORTA_ASBR OPT_RT_E
#define ORTA_ABR  OPT_RT_B
  /*
   * For ORT_NET routes, the field is almost unused with one
   * exception: ORTA_PREF for external routes means that the route is
   * preferred in AS external route selection according to 16.4.1. -
   * it is intra-area path using non-backbone area. In other words,
   * the forwarding address (or ASBR if forwarding address is zero) is
   * intra-area (type == RTS_OSPF) and its area is not a backbone.
   */
#define ORTA_PREF 0x80000000
  u32 metric1;
  u32 metric2;
  u32 tag;
  u32 rid;			/* Router ID of real advertising router */
  struct ospf_area *oa;
  struct ospf_iface *ifa;	/* Outgoing interface */
  ip_addr nh;			/* Next hop */
}
orta;

typedef struct ort
{
  struct fib_node fn;
  orta n;
  orta o;
}
ort;

void ospf_rt_spf(struct proto_ospf *po);
void ospf_rt_initort(struct fib_node *fn);


#endif /* _BIRD_OSPF_RT_H_ */
