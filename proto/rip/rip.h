/*
 * Structures for RIP protocol
 */

struct rip_connection {
  node n;

  int num;
  struct proto *proto;
  ip_addr addr;
  struct rip_entry *sendptr;
  sock *send;
  struct rip_interface *rif;

  ip_addr daddr;
  int dport;
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

struct rip_block {
  u16 family;	/* 0xffff on first message means this is authentication */
  u16 tag;
  ip_addr network;
  ip_addr netmask;
  ip_addr nexthop;
  u32 metric;
};

struct rip_entry {
  node n;

  ip_addr whotoldme;
  ip_addr network;
  int pxlen;
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

  int metric;		/* User configurable data */
};

struct rip_proto {
  struct proto inherited;
  timer *timer;
  list connections;
  list rtable;
  list garbage;
  list interfaces;	/* Interfaces we really know about */
  list iface_list;	/* Patterns configured */
  int magic;

  int infinity;		/* User configurable data */
  int port;
  int period;
  int garbage_time;
};

#define P ((struct rip_proto *) p)
#define E ((struct rip_entry *) e)

#define RIP_MAGIC 81861253
#define CHK_MAGIC do { if (P->magic != RIP_MAGIC) bug( "Not enough magic\n" ); } while (0)

void rip_init_instance(struct proto *p);
