/*
 *	BIRD -- OSPF
 *
 *	(c) 1999 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_OSPF_H_
#define _BIRD_OSPF_H_

#define LOCAL_DEBUG

#define IAMMASTER(x) ((x) & DBDES_MS)
#define INISET(x) ((x) & DBDES_I)

#include <string.h>

#include "nest/bird.h"

#include "lib/checksum.h"
#include "lib/ip.h"
#include "lib/lists.h"
#include "lib/socket.h"
#include "lib/timer.h"
#include "lib/resource.h"
#include "nest/protocol.h"
#include "nest/iface.h"
#include "nest/route.h"
#include "conf/conf.h"

#define OSPF_PROTO 89
#ifndef IPV6
#define OSPF_VERSION 2
#define AllSPFRouters ipa_from_u32(0xe0000005)	/* 224.0.0.5 */
#define AllDRouters ipa_from_u32(0xe0000006)	/* 224.0.0.6 */
#else
#error Multicast address not defined in IPv6
#endif


#define LSREFRESHTIME 1800	/* 30 minut */
#define MINLSINTERVAL 5
#define MINLSARRIVAL 1
#define MAXAGE 3600		/* 1 hour */
#define CHECKAGE 300		/* 5 min */
#define MAXAGEDIFF 900		/* 15 min */
#define LSINFINITY 0xffffff
/*#define DEFAULTDES 0.0.0.0 FIXME: How to define it? */
#define INITSEQNUM 0x80000001	/* Initial Sequence Number */
#define MAXSEQNUM 0x7fffffff	/* Maximal Sequence Number */

struct ospf_config {
  struct proto_config c;
  u32 area;		/* FIXME: Area ID  !!! This is wrong !!!
                         * Should respect interface */
};

struct ospf_iface {
  node n;
  struct proto_ospf *proto;
  struct iface *iface;	/* Nest's iface */
  sock *hello_sk;	/* Hello socket */
  sock *ip_sk;		/* IP socket (for DD ...) */
  list neigh_list;	/* List of neigbours */
  u32 area;		/* OSPF Area */
  u16 cost;		/* Cost of iface */
  u16 rxmtint;		/* number of seconds between LSA retransmissions */
  u16 iftransdelay;	/* The estimated number of seconds it takes to
			   transmit a Link State Update Packet over this
			   interface.  LSAs contained in the update */
  u8 priority;		/* A router priority for DR election */
  u16 helloint;		/* number of seconds between hello sending */
  u16 waitint;		/* number of sec before changing state from wait */
  u32 deadc;		/* after "deadint" missing hellos is router dead */
  u16 autype;
  u8 aukey[8];
  u8 options;
  ip_addr drip;		/* Designated router */
  u32 drid;
  ip_addr bdrip;	/* Backup DR */
  u32 bdrid;
  u8 type;		/* OSPF view of type */
#define OSPF_IT_BCAST 0
#define OSPF_IT_NBMA 1
#define OSPF_IT_PTP 2
#define OSPF_IT_VLINK 3
  u8 state;		/* Interface state machine */
#define OSPF_IS_DOWN 0		/* Not working */
#define OSPF_IS_LOOP 1		/* Should never happen */
#define OSPF_IS_WAITING 2	/* Waiting for Wait timer */
#define OSPF_IS_PTP 3		/* PTP operational */
#define OSPF_IS_DROTHER 4	/* I'm on BCAST or NBMA and I'm not DR */
#define OSPF_IS_BACKUP 5	/* I'm BDR */
#define OSPF_IS_DR 6		/* I'm DR */
  timer *wait_timer;		/* WAIT timer */
  timer *hello_timer;		/* HELLOINT timer */
  timer *rxmt_timer;		/* RXMT timer */
/* Default values for interface parameters */
#define COST_D 10
#define RXMTINT_D 5
#define IFTRANSDELAY_D 1
#define PRIORITY_D 1
#define HELLOINT_D 10
#define DEADC_D 4
#define WAIT_DMH 3	/* Value of Wait timer - not found it in RFC 
			 * - using 3*HELLO
			 */
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
  u16 helloint;
  u8 options;
  u8 priority;
  u32 deadint;
  u32 dr;
  u32 bdr;
};

struct ospf_dbdes_packet {
  struct ospf_packet ospf_packet;
  u16 iface_mtu;
  u8 options;
  u8 imms;		/* I, M, MS bits */
#define DBDES_MS 1
#define DBDES_M 2
#define DBDES_I 4
  u32 ddseq;
};

struct ospf_lsaheader {
  u16 lsage;		/* LS Age */
  u8 options;
  u8 lstype;
  u32 lsid;		
  u32 advr;		/* Advertising router */
  u32 lssn;		/* LS Sequence number */
  u16 checksum;
  u16 length;
};


struct ospf_neighbor
{
  node n;
  struct ospf_iface *ifa;
  u8 state;
#define NEIGHBOR_DOWN 0
#define NEIGHBOR_ATTEMPT 1
#define NEIGHBOR_INIT 2
#define NEIGHBOR_2WAY 3
#define NEIGHBOR_EXSTART 4
#define NEIGHBOR_EXCHANGE 5
#define NEIGHBOR_LOADING 6
#define NEIGHBOR_FULL 7
  timer *inactim;	/* Inactivity timer */
  u8 imms;		/* I, M, Master/slave received */
  u32 dds;		/* DD Sequence number being sent */
  u32 ddr;		/* last Dat Des packet received */
  u8 myimms;		/* I, M Master/slave */
  u32 rid;		/* Router ID */
  ip_addr ip;		/* IP of it's interface */
  u8 priority;		/* Priority */
  u8 options;		/* Options received */
  u32 dr;		/* Neigbour's idea of DR */
  u32 bdr;		/* Neigbour's idea of BDR */
  u8 adj;		/* built adjacency? */
};

/* Definitions for interface state machine */
#define ISM_UP 0	/* Interface Up */
#define ISM_WAITF 1	/* Wait timer fired */
#define ISM_BACKS 2	/* Backup seen */
#define ISM_NEICH 3	/* Neighbor change */
#define ISM_LOOP 4	/* Loop indicated */
#define ISM_UNLOOP 5	/* Unloop indicated */
#define ISM_DOWN 6	/* Interface down */

/* Definitions for neighbor state machine */
#define INM_HELLOREC 0	/* Hello Received */
#define INM_START 1	/* Neighbor start - for NBMA */
#define INM_2WAYREC 2	/* 2-Way received */
#define INM_NEGDONE 3	/* Negotiation done */
#define INM_EXDONE 4	/* Exchange done */
#define INM_BADLSREQ 5	/* Bad LS Request */
#define INM_LOADDONE 6	/* Load done */
#define INM_ADJOK 7	/* AdjOK? */
#define INM_SEQMIS 8	/* Sequence number mismatch */
#define INM_1WAYREC 9	/* 1-Way */
#define INM_KILLNBR 10	/* Kill Neighbor */
#define INM_INACTTIM 11	/* Inactivity timer */
#define INM_LLDOWN 12	/* Line down */

struct ospf_area {
  struct ospf_area *next;
  u32 areaid;
  struct top_graph *gr;		/* LSA graph */
  struct top_hash_entry *rt;	/* My own router LSA */
  slab *rtlinks;
};

struct proto_ospf {
  struct proto proto;
  list iface_list;		/* Interfaces we really use */
  int areano;			/* Number of area I belong to */
  struct ospf_area *firstarea;
};

static int ospf_start(struct proto *p);
static void ospf_dump(struct proto *p);
static struct proto *ospf_init(struct proto_config *c);
static void ospf_preconfig(struct protocol *p, struct config *c);
static void ospf_postconfig(struct proto_config *c);

#include "proto/ospf/hello.h"
#include "proto/ospf/packet.h"
#include "proto/ospf/iface.h"
#include "proto/ospf/neighbor.h"
#include "proto/ospf/topology.h"
#include "proto/ospf/dbdes.h"

#endif /* _BIRD_OSPF_H_ */
