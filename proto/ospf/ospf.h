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
#define AllSPFRouters ipa_from_u32(0xe0000005)	/* 224.0.0.5 */
#define AllDRouters ipa_from_u32(0xe0000006)	/* 224.0.0.6 */
#else
#error Multicast address not defined
#endif


struct ospf_config {
  struct proto_config c;
  u32 area;		/* FIXME: Area ID  !!! This is wrong !!!
                         * Should respect interface */
};

struct ospf_iface {
  node n;
  struct iface *iface;	/* Nest's iface */
  list sk_list;		/* List of active sockets */
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
  timer *wait_timer;		/* One shot Wait timer - used after DOWN->UP */

/* Default values for interface parameters */
#define COST_D 10
#define RXMTINT_D 5
#define IFTRANSDELAY_D 1
#define PRIORITY_D 1
#define HELLOINT_D 10
#define DEADINT_D 4
#define WAIT_D 40	/* Value of Wait timer - I didn't found it in RFC */

};

struct ospf_sock {
  node n;
  sock *sk;
};

struct ospf_patt {
  struct iface_patt i;

  u16 cost;
  byte mode;
};

struct proto_ospf {
  struct proto proto;
  list iface_list;		/* Interfaces we really use */
};

#endif /* _BIRD_OSPF_H_ */
