/*
 *	BIRD -- OSPF
 *
 *	(c) 1999 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_OSPF_TOPOLOGY_H_
#define _BIRD_OSPF_TOPOLOGY_H_

struct top_hash_entry {  /* Index for fast mapping (type,rtrid,LSid)->vertex */
  struct top_hash_entry *next;		/* Next in hash chain */
  struct top_vertex *vertex;
  u32 lsa_id, rtr_id;
  u8 lsa_type;
  u8 options;
  u16 lsage;
  u32 lsseqno;
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
struct top_hash_entry *ospf_hash_find(struct top_graph *, u32 lsa, u32 rtr, u32 type);
struct top_hash_entry *ospf_hash_get(struct top_graph *, u32 lsa, u32 rtr, u32 type);
void ospf_hash_delete(struct top_graph *, struct top_hash_entry *);
void addifa_rtlsa(struct ospf_iface *ifa);

struct top_graph_rtlsa {
  u8 Vbit;
  u8 Ebit;
  u8 Bbit;
  int links;		/* Number of links */
  struct top_graph_rtlsa_link *flink;
};

struct top_graph_rtlsa_link {	/* FIXME Completely ignoring TOS */
  u32 id;
  u32 data;
  u8 type;
  u16 metric;
  struct top_graph_rtlsa_link *next;
};


#endif /* _BIRD_OSPF_TOPOLOGY_H_ */
