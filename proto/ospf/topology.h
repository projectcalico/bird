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
  u16 pad1;
  u8 pad2;
};

struct top_vertex {	/* LSA without type,rtid and lsid */
  u16 lsage;
  u8 options;
  u32 lsseqno;
  void *data;
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


#endif /* _BIRD_OSPF_TOPOLOGY_H_ */
