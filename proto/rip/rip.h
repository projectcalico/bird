/*
 * Structures for RIP protocol
 *
   FIXME: in V6, they insert additional entry whenever next hop differs. Such entry is identified by 0xff in metric.
 */

#include "nest/route.h"
#include "nest/password.h"

#define EA_RIP_TAG	EA_CODE(EAP_RIP, 0)
#define EA_RIP_METRIC	EA_CODE(EAP_RIP, 1)

#define PACKET_MAX 25
#define PACKET_MD5_MAX 18	/* FIXME */

struct rip_connection {
  node n;

  int num;
  struct proto *proto;
  ip_addr addr;
  sock *send;
  struct rip_interface *rif;
  struct fib_iterator iter;

  ip_addr daddr;
  int dport;
  int done;
};

struct rip_packet_heading {
  u8 command;
#define RIPCMD_REQUEST          1       /* want info */
#define RIPCMD_RESPONSE         2       /* responding to request */
#define RIPCMD_TRACEON          3       /* turn tracing on */
#define RIPCMD_TRACEOFF         4       /* turn it off */
#define RIPCMD_MAX              5
  u8 version;
#define RIP_V1 1
#define RIP_V2 2
  u16 unused;
};

#ifndef IPV6
struct rip_block {
  u16 family;	/* 0xffff on first message means this is authentication */
  u16 tag;
  ip_addr network;
  ip_addr netmask;
  ip_addr nexthop;
  u32 metric;
};
#else
struct rip_block {
  ip_addr network;
  u16 tag;
  u8 pxlen;
  u8 metric
};
#endif

struct rip_block_auth {
  u16 mustbeFFFF;
  u16 authtype;
  u16 packetlen;
  u8 keyid;
  u8 authlen;
  u32 seq;
  u32 zero0;
  u32 zero1;
};

struct rip_md5_tail {
  u16 mustbeFFFF;
  u16 mustbe0001;
  char md5[16];
};

struct rip_entry {
  struct fib_node n;

  ip_addr whotoldme;
  ip_addr nexthop;
  int metric;
  u16 tag;

  bird_clock_t updated, changed;
  int flags;
#define RIP_F_EXTERNAL 1
};

struct rip_packet {
  struct rip_packet_heading heading;
  struct rip_block block[25];
};

struct rip_interface {
  node n;
  struct proto *proto;
  struct iface *iface;
  sock *sock;
  struct rip_connection *busy;
  struct rip_patt *patt;  
  int triggered;
};

struct rip_patt {
  struct iface_patt i;

  int metric;
  int mode;
#define IM_MULTICAST 1
#define IM_BROADCAST 2
#define IM_QUIET 4
#define IM_NOLISTEN 8
#define IM_VERSION1 16
};

struct rip_proto_config {
  struct proto_config c;
  list iface_list;	/* Patterns configured */

  int infinity;		/* User configurable data */
  int port;
  int period;
  int garbage_time;
  int timeout_time;

  struct password_item *passwords;
  int authtype;
#define AT_NONE 0
#define AT_PLAINTEXT 2
#define AT_MD5 3
  int honour;
#define HO_NEVER 0
#define HO_NEIGHBOUR 1
#define HO_ALWAYS 2
};

struct rip_proto {
  struct proto inherited;
  timer *timer;
  list connections;
  struct fib rtable;
  list garbage;
  list interfaces;	/* Interfaces we really know about */
  int magic;
  int tx_count;		/* Do one regular update once in a while */
};


#define RIP_MAGIC 81861253
#define CHK_MAGIC do { if (P->magic != RIP_MAGIC) bug( "Not enough magic\n" ); } while (0)

void rip_init_instance(struct proto *p);
void rip_init_config(struct rip_proto_config *c);

/* Authentication functions */

int rip_incoming_authentication( struct proto *p, struct rip_block_auth *block, struct rip_packet *packet, int num );
void rip_outgoing_authentication( struct proto *p, struct rip_block_auth *block, struct rip_packet *packet, int num );
