/*
 *	BIRD -- OSPF
 *
 *	(c) 1999 - 2000 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_OSPF_TOPOLOGY_H_
#define _BIRD_OSPF_TOPOLOGY_H_

struct top_hash_entry {  /* Index for fast mapping (type,rtrid,LSid)->vertex */
   snode n;
   node cn;
  struct top_hash_entry *next;		/* Next in hash chain */
  struct ospf_lsa_header lsa;
  void *lsa_body;
  bird_clock_t inst_t;			/* Time of installation into DB */
  ip_addr nh;				/* Next hop */
  struct iface *nhi;
  u16 dist;				/* Distance from the root */
  u8 color;
#define OUTSPF 0
#define CANDIDATE 1
#define INSPF 2
  u8 padding;
};

struct top_graph {
  pool *pool;				/* Pool we allocate from */
  slab *hash_slab;			/* Slab for hash entries */
  struct top_hash_entry **hash_table;	/* Hashing (modelled a`la fib) */
  unsigned int hash_size;
  unsigned int hash_order;
  unsigned int hash_mask;
  unsigned int hash_entries;
  unsigned int hash_entries_min, hash_entries_max;
};

struct top_graph *ospf_top_new(struct proto_ospf *);
void ospf_top_free(struct top_graph *);
void ospf_top_dump(struct top_graph *);
struct top_hash_entry *ospf_hash_find_header(struct top_graph *f, struct ospf_lsa_header *h);
struct top_hash_entry *ospf_hash_get_header(struct top_graph *f, struct ospf_lsa_header *h);
struct top_hash_entry *ospf_hash_find(struct top_graph *, u32 lsa, u32 rtr, u32 type);
struct top_hash_entry *ospf_hash_get(struct top_graph *, u32 lsa, u32 rtr, u32 type);
void ospf_hash_delete(struct top_graph *, struct top_hash_entry *);
void addifa_rtlsa(struct ospf_iface *ifa);
void originate_rt_lsa(struct ospf_area *oa);
void originate_net_lsa(struct ospf_iface *ifa,struct proto_ospf *po);
int can_flush_lsa(struct ospf_area *oa);
void originate_ext_lsa(net *n, rte *e, struct proto_ospf *po, struct ea_list *attrs);

#endif /* _BIRD_OSPF_TOPOLOGY_H_ */
