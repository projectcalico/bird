/*
 *	BIRD -- The Border Gateway Protocol
 *
 *	(c) 2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_BGP_H_
#define _BIRD_BGP_H_

#include "nest/route.h"

struct linpool;
struct eattr;

struct bgp_config {
  struct proto_config c;
  unsigned int local_as, remote_as;
  ip_addr remote_ip;
  int multihop;				/* Number of hops if multihop */
  ip_addr multihop_via;			/* Multihop: address to route to */
  int next_hop_self;			/* Always set next hop to local IP address */
  unsigned connect_retry_time;
  unsigned hold_time, initial_hold_time;
  unsigned keepalive_time;
};

struct bgp_conn {
  struct bgp_proto *bgp;
  struct birdsock *sk;
  unsigned int state;			/* State of connection state machine */
  struct timer *connect_retry_timer;
  struct timer *hold_timer;
  struct timer *keepalive_timer;
  int packets_to_send;			/* Bitmap of packet types to be sent */
  int notify_code, notify_subcode, notify_arg, notify_arg_size;
  int error_flag;			/* Error state, ignore all input */
  int primary;				/* This connection is primary */
  unsigned hold_time, keepalive_time;	/* Times calculated from my and neighbor's requirements */
};

struct bgp_proto {
  struct proto p;
  struct bgp_config *cf;		/* Shortcut to BGP configuration */
  node bgp_node;			/* Node in global BGP protocol list */
  unsigned local_as, remote_as;
  int is_internal;			/* Internal BGP connection (local_as == remote_as) */
  u32 local_id;				/* BGP identifier of this router */
  u32 remote_id;			/* BGP identifier of the neighbor */
  struct bgp_conn *conn;		/* Connection we have established */
  struct bgp_conn outgoing_conn;	/* Outgoing connection we're working with */
  struct bgp_conn incoming_conn;	/* Incoming connection we have neither accepted nor rejected yet */
  struct object_lock *lock;		/* Lock for neighbor connection */
  ip_addr next_hop;			/* Either the peer or multihop_via */
  struct neighbor *neigh;		/* Neighbor entry corresponding to next_hop */
  ip_addr local_addr;			/* Address of the local end of the link to next_hop */
  struct bgp_bucket **bucket_hash;	/* Hash table of attribute buckets */
  unsigned int hash_size, hash_count, hash_limit;
  struct fib prefix_fib;		/* Prefixes to be sent */
  list bucket_queue;			/* Queue of buckets to send */
  struct bgp_bucket *withdraw_bucket;	/* Withdrawn routes */
};

struct bgp_prefix {
  struct fib_node n;			/* Node in prefix fib */
  node bucket_node;			/* Node in per-bucket list */
};

struct bgp_bucket {
  struct bgp_bucket *hash_next, *hash_prev;	/* Node in bucket hash table */
  node send_node;			/* Node in send queue */
  unsigned hash;			/* Hash over extended attributes */
  list prefixes;			/* Prefixes in this buckets */
  ea_list eattrs[0];			/* Per-bucket extended attributes */
};

#define BGP_PORT		179
#define BGP_VERSION		4
#define BGP_HEADER_LENGTH	19
#define BGP_MAX_PACKET_LENGTH	4096
#define BGP_RX_BUFFER_SIZE	4096
#define BGP_TX_BUFFER_SIZE	BGP_MAX_PACKET_LENGTH

extern struct linpool *bgp_linpool;

void bgp_start_timer(struct timer *t, int value);
void bgp_check(struct bgp_config *c);
void bgp_error(struct bgp_conn *c, unsigned code, unsigned subcode, unsigned data, unsigned len);
void bgp_close_conn(struct bgp_conn *c);

/* attrs.c */

struct rta *bgp_decode_attrs(struct bgp_conn *conn, byte *a, unsigned int len, struct linpool *pool);
int bgp_get_attr(struct eattr *e, byte *buf);
int bgp_rte_better(struct rte *, struct rte *);
void bgp_rt_notify(struct proto *, struct network *, struct rte *, struct rte *, struct ea_list *);
int bgp_import_control(struct proto *, struct rte **, struct ea_list **, struct linpool *);
struct ea_list *bgp_path_prepend(struct linpool *pool, struct eattr *a, struct ea_list *old, int as);
void bgp_attr_init(struct bgp_proto *);

/* packets.c */

void bgp_schedule_packet(struct bgp_conn *conn, int type);
void bgp_tx(struct birdsock *sk);
int bgp_rx(struct birdsock *sk, int size);

/* Packet types */

#define PKT_OPEN		0x01
#define PKT_UPDATE		0x02
#define PKT_NOTIFICATION	0x03
#define PKT_KEEPALIVE		0x04
#define PKT_SCHEDULE_CLOSE	0x1f	/* Used internally to schedule socket close */

/* Attributes */

#define BAF_OPTIONAL		0x80
#define BAF_TRANSITIVE		0x40
#define BAF_PARTIAL		0x20
#define BAF_EXT_LEN		0x10

#define BA_ORIGIN		0x01	/* [RFC1771] */		/* WM */
#define BA_AS_PATH		0x02				/* WM */
#define BA_NEXT_HOP		0x03				/* WM */
#define BA_MULTI_EXIT_DISC	0x04				/* ON */
#define BA_LOCAL_PREF		0x05				/* WD */
#define BA_ATOMIC_AGGR		0x06				/* WD */
#define BA_AGGREGATOR		0x07				/* OT */
#define BA_COMMUNITY		0x08	/* [RFC1997] */		/* OT */
#define BA_ORIGINATOR_ID	0x09	/* [RFC1966] */		/* ON */
#define BA_CLUSTER_LIST		0x0a				/* ON */
/* We don't support these: */
#define BA_DPA			0x0b	/* ??? */
#define BA_ADVERTISER		0x0c	/* [RFC1863] */
#define BA_RCID_PATH		0x0d
#define BA_MP_REACH_NLRI	0x0e	/* [RFC2283] */
#define BA_MP_UNREACH_NLRI	0x0f
#define BA_EXTENDED_COMM	0x10	/* draft-ramachandra-bgp-ext-communities */

#define BGP_PATH_AS_SET		1	/* Types of path segments */
#define BGP_PATH_AS_SEQUENCE	2

/* BGP states */

#define BS_IDLE			0
#define BS_CONNECT		1	/* Attempting to connect */
#define BS_ACTIVE		2	/* Waiting for connection retry & listening */
#define BS_OPENSENT		3
#define BS_OPENCONFIRM		4
#define BS_ESTABLISHED		5

#endif
