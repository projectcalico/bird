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
};

struct rip_packet {
  struct rip_packet_heading heading;
  struct rip_block block[25];
};

struct rip_data {
  struct proto inherited;
  sock *listen;
  timer *timer;
  list connections;
  list rtable;
  int magic;
};

#define P ((struct rip_data *) p)
#define E ((struct rip_entry *) e)

#define RIP_MAGIC 81861253
#define CHK_MAGIC do { if (P->magic != RIP_MAGIC) die( "Not enough magic\n" ); } while (0)
