/*
 *	BIRD -- OSPF
 *
 *	(c) 1999 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_OSPF_H_
#define _BIRD_OSPF_H_

#define OSPF_PROTO 89
#ifndef IPV6
#define OSPF_VERSION 2
#define AllSPFRouters ipa_from_u32(0xe0000005)	/* 224.0.0.5 */
#define AllDRouters ipa_from_u32(0xe0000006)	/* 224.0.0.6 */
#else
#error Multicast address not defined in IPv6
#endif

struct proto_ospf {
  struct proto proto;
  list iface_list;		/* Interfaces we really use */
};

struct ospf_config {
  struct proto_config c;
  u32 area;		/* FIXME: Area ID  !!! This is wrong !!!
                         * Should respect interface */
};

struct ospf_iface {
  node n;
  struct proto_ospf *proto;
  struct iface *iface;	/* Nest's iface */
  sock *hello_sk;	/* List of active sockets */
  list neigh_list;	/* List of neigbours */
  u32 area;		/* OSPF Area */
  u16 cost;		/* Cost of iface */
  int rxmtint;		/* number of seconds between LSA retransmissions */
  int iftransdelay;	/* The estimated number of seconds it takes to
			   transmit a Link State Update Packet over this
			   interface.  LSAs contained in the update */
  u8 priority;		/* A router priority for DR election */
  u16 helloint;		/* number of seconds between hello sending */
  u32 deadint;		/* after "deadint" missing hellos is router dead */
  u16 autype;
  u8 aukey[8];
  u8 options;
  ip_addr drip;		/* Designated router */
  u32 drid;
  ip_addr bdrip;	/* Backup DR */
  u32 bdrid;
  int type;		/* OSPF view of type */
#define OSPF_IT_BROADCAST 0
#define OSPF_IT_NBMA 1
#define OSPF_IT_PTP 2
  int state;		/* Interface state machine */
#define OSPF_IS_DOWN 0		/* Should never happen */
#define OSPF_IS_WAITING 1	/* Waiting for Wait timer */
#define OSPF_IS_PTP 2		/* PTP operational */
#define OSPF_IS_DROTHER 3	/* I'm on BCAST or NBMA and I'm not DR */
#define OSPF_IS_BACKUP 4	/* I'm BDR */
#define OSPF_IS_DR 5		/* I'm DR */
  timer *wait_timer;		/* WAIT timer */
  timer *hello_timer;		/* HELLOINT timer */
/* Default values for interface parameters */
#define COST_D 10
#define RXMTINT_D 5
#define IFTRANSDELAY_D 1
#define PRIORITY_D 1
#define HELLOINT_D 10
#define DEADINT_D 4
#define WAIT_DMH 2	/* Value of Wait timer - not found it in RFC - using 2*HELLO */
};

struct ospf_patt {
  struct iface_patt i;

  u16 cost;
  byte mode;
};

struct ospf_packet {
  u8 version;
  u8 type;
#define HELLO 1 /* Hello */
#define DBDES 2 /* Database description */
#define LSREQ 3 /* Link state request */
#define LSUPD 4 /* Link state update */
#define LSACK 5 /* Link state acknowledgement */
  u16 length;
  u32 routerid;
  u32 areaid;
  u16 checksum;
  u16 autype;
  u8 authetication[8];
};

struct ospf_hello_packet {
  struct ospf_packet ospf_packet;
  ip_addr netmask;
  u16 hello_int;
  u8 options;
  u8 priority;
  u32 deadint;
  u32 dr;
  u32 bdr;
};

struct ospf_neighbor
{
  struct ospf_iface *ifa;
  int state;
#define NEIGHBOR_DOWN 0
#define NEIGHBOR_INIT 1
#define NEIGHBOR_2WAY 2
#define NEIGHBOR_ATTEMPT 3
#define NEIGHBOR_EXSTART 4
#define NEIGHBOR_EXCHANGE 5
#define NEIGHBOR_LOADING 6
#define NEIGHBOR_FULL 7
  timer *inactim;	/* Inactivity timer */
  byte ms;		/* Master/slave */
  u32 dds;		/* DD Sequence number being sentg */
  u32 ddr;		/* last Dat Des packet */
  u32 rid;		/* Router ID */
  byte pri;		/* Priority */
  byte options;		/* Options */
  u32 dr;		/* Neigbour's idea of DR */
  u32 bdr;		/* Neigbour's idea of BDR */
};

#endif /* _BIRD_OSPF_H_ */
