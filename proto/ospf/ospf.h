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
  u32 area;		/* Area ID  !!! This is wrong !!!
                         * Should respect interface */
  list iface_list;
};

struct ospf_iface {
  node n;
  list sk_list;		/* List of active sockets */
  struct iface *iface;	/* Nest's iface */
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
  int type;
#define OSPF_IM_BROADCAST 0
#define OSPF_IM_NBMA 1
#define OSPF_IM_PTP 2

/* Default values for interface parameters */
#define COST_D 10
#define RXMTINT_D 5
#define IFTRANSDELAY_D 1
#define PRIORITY_D 0
#define HELLOINT_D 10
#define DEADINT_D 4
};


struct ospf_patt {
  struct iface_patt i;

  u16 cost;
  byte mode;
};


#endif /* _BIRD_OSPF_H_ */
