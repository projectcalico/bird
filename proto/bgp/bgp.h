/*
 *	BIRD -- The Border Gateway Protocol
 *
 *	(c) 2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_BGP_H_
#define _BIRD_BGP_H_

struct linpool;
struct eattr;

struct bgp_config {
  struct proto_config c;
  unsigned int local_as, remote_as;
  ip_addr remote_ip;
  int multihop;				/* Number of hops if multihop */
  ip_addr multihop_via;			/* Multihop: address to route to */
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
