/*
 *	BIRD -- OSPF
 *
 *	(c) 1999--2004 Ondrej Filip <feela@network.cz>
 *	(c) 2009--2014 Ondrej Zajicek <santiago@crfreenet.org>
 *	(c) 2009--2014 CZ.NIC z.s.p.o.
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_OSPF_TOPOLOGY_H_
#define _BIRD_OSPF_TOPOLOGY_H_

struct top_hash_entry
{				/* Index for fast mapping (type,rtrid,LSid)->vertex */
  snode n;
  node cn;			/* For adding into list of candidates
				   in intra-area routing table calculation */
  struct top_hash_entry *next;	/* Next in hash chain */
  struct ospf_lsa_header lsa;
  u16 lsa_type;			/* lsa.type processed and converted to common values */	
  u16 init_age;			/* Initial value for lsa.age during inst_time */
  u32 domain;			/* Area ID for area-wide LSAs, Iface ID for link-wide LSAs */
  //  struct ospf_area *oa;
  void *lsa_body;		/* May be NULL if LSA was flushed but hash entry was kept */
  void *next_lsa_body;		/* For postponed LSA origination */
  u16 next_lsa_blen;		/* For postponed LSA origination */
  u16 next_lsa_opts;		/* For postponed LSA origination */
  bird_clock_t inst_time;	/* Time of installation into DB */
  struct ort *nf;		/* Reference fibnode for sum and ext LSAs, NULL for otherwise */
  struct mpnh *nhs;		/* Computed nexthops - valid only in ospf_rt_spf() */
  ip_addr lb;			/* In OSPFv2, link back address. In OSPFv3, any global address in the area useful for vlinks */
  u32 lb_id;			/* Interface ID of link back iface (for bcast or NBMA networks) */
  u32 dist;			/* Distance from the root */
  int ret_count;		/* Number of retransmission lists referencing the entry */
  u8 color;
#define OUTSPF 0
#define CANDIDATE 1
#define INSPF 2
  u8 rtcalc;			/* LSA generated during RT calculation (LSA_RTCALC or LSA_STALE)*/
  u8 nhs_reuse;			/* Whether nhs nodes can be reused during merging.
				   See a note in rt.c:merge_nexthops() */
};

#define LSA_RTCALC	1
#define LSA_STALE	2

struct top_graph
{
  pool *pool;			/* Pool we allocate from */
  slab *hash_slab;		/* Slab for hash entries */
  struct top_hash_entry **hash_table;	/* Hashing (modelled a`la fib) */
  uint ospf2;			/* Whether it is for OSPFv2 or OSPFv3 */
  uint hash_size;
  uint hash_order;
  uint hash_mask;
  uint hash_entries;
  uint hash_entries_min, hash_entries_max;
};

struct ospf_new_lsa
{
  u16 type;
  u32 dom;
  u32 id;
  u16 opts;
  u16 length;
  struct ospf_iface *ifa;
  struct ort *nf;
};

struct top_graph *ospf_top_new(pool *);
void ospf_top_free(struct top_graph *);
void ospf_top_dump(struct top_graph *, struct proto *);

struct top_hash_entry * ospf_install_lsa(struct ospf_proto *p, struct ospf_lsa_header *lsa, u32 type, u32 domain, void *body);
struct top_hash_entry * ospf_originate_lsa(struct ospf_proto *p, struct ospf_new_lsa *lsa);
void ospf_advance_lsa(struct ospf_proto *p, struct top_hash_entry *en, struct ospf_lsa_header *lsa, u32 type, u32 domain, void *body);
void ospf_flush_lsa(struct ospf_proto *p, struct top_hash_entry *en);
void ospf_update_lsadb(struct ospf_proto *p);

static inline void ospf_flush2_lsa(struct ospf_proto *p, struct top_hash_entry **en)
{ if (*en) { ospf_flush_lsa(p, *en); *en = NULL; } }

void ospf_originate_sum_net_lsa(struct ospf_proto *p, struct ospf_area *oa, ort *nf, int metric);
void ospf_originate_sum_rt_lsa(struct ospf_proto *p, struct ospf_area *oa, ort *nf, int metric, u32 options);
void ospf_originate_ext_lsa(struct ospf_proto *p, struct ospf_area *oa, ort *nf, u8 rtcalc, u32 metric, u32 ebit, ip_addr fwaddr, u32 tag, int pbit);

void ospf_rt_notify(struct proto *P, rtable *tbl, net *n, rte *new, rte *old, ea_list *attrs);
void ospf_update_topology(struct ospf_proto *p);

struct top_hash_entry *ospf_hash_find(struct top_graph *, u32 domain, u32 lsa, u32 rtr, u32 type);
struct top_hash_entry *ospf_hash_get(struct top_graph *, u32 domain, u32 lsa, u32 rtr, u32 type);
void ospf_hash_delete(struct top_graph *, struct top_hash_entry *);

static inline struct top_hash_entry * ospf_hash_find_entry(struct top_graph *f, struct top_hash_entry *en)
{ return ospf_hash_find(f, en->domain, en->lsa.id, en->lsa.rt, en->lsa_type); }

static inline struct top_hash_entry * ospf_hash_get_entry(struct top_graph *f, struct top_hash_entry *en)
{ return ospf_hash_get(f, en->domain, en->lsa.id, en->lsa.rt, en->lsa_type); }

struct top_hash_entry * ospf_hash_find_rt(struct top_graph *f, u32 domain, u32 rtr);
struct top_hash_entry * ospf_hash_find_rt3_first(struct top_graph *f, u32 domain, u32 rtr);
struct top_hash_entry * ospf_hash_find_rt3_next(struct top_hash_entry *e);

struct top_hash_entry * ospf_hash_find_net2(struct top_graph *f, u32 domain, u32 id);

/* In OSPFv2, id is network IP prefix (lsa.id) while lsa.rt field is unknown
   In OSPFv3, id is lsa.rt of DR while nif is neighbor iface id (lsa.id) */
static inline struct top_hash_entry *
ospf_hash_find_net(struct top_graph *f, u32 domain, u32 id, u32 nif)
{
  return f->ospf2 ?
    ospf_hash_find_net2(f, domain, id) :
    ospf_hash_find(f, domain, nif, id, LSA_T_NET);
}


#endif /* _BIRD_OSPF_TOPOLOGY_H_ */
