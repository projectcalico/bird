
#ifndef _BIRD_BFD_H_
#define _BIRD_BFD_H_

#define BFD_CONTROL_PORT	3784
#define BFD_ECHO_PORT		3785
#define BFD_MULTI_CTL_PORT	4784

#define BFD_DEFAULT_MIN_RX_INT	(10 MS)
#define BFD_DEFAULT_MIN_TX_INT	(100 MS)
#define BFD_DEFAULT_IDLE_TX_INT	(1 S)
#define BFD_DEFAULT_MULTIPLIER	5


struct bfd_config
{
  struct proto_config c;
  list neighbors;		/* List of struct bfd_neighbor */
};

struct bfd_session_config
{
  u32 min_rx_int;
  u32 min_tx_int;
  u32 idle_tx_int;
  u8 multiplier;
  u8 multihop;
  u8 passive;
};

struct bfd_neighbor
{
  node n;
  ip_addr addr;
  ip_addr local;
  struct iface *iface;
  struct bfd_session_config *opts;

  struct bfd_session *session;
};

struct bfd_proto
{
  struct proto p;

  slab *session_slab;
  HASH(struct bfd_session) session_hash_id;
  HASH(struct bfd_session) session_hash_ip;

  list sockets;
};

struct bfd_socket
{
  node n;
  sock *sk;
  u32 uc;
};

struct bfd_session
{
  node n;
  struct bfd_session *next_id;		/* Next in bfd.session_hash_id */
  struct bfd_session *next_ip;		/* Next in bfd.session_hash_ip */

  u8 opened;
  u8 poll_active;
  u8 poll_scheduled;
  
  u8 loc_state;
  u8 rem_state;
  u8 loc_diag;
  u32 loc_id;				/* Local session ID (local discriminator) */
  u32 rem_id;				/* Remote session ID (remote discriminator) */
  u32 des_min_tx_int;			/* Desired min rx interval, local option */
  u32 des_min_tx_new;			/* Used for des_min_tx_int change */
  u32 req_min_rx_int;			/* Required min tx interval, local option */
  u32 req_min_rx_new;			/* Used for req_min_rx_int change */
  u32 rem_min_tx_int;			/* Last received des_min_tx_int */
  u32 rem_min_rx_int;			/* Last received req_min_rx_int */
  u8 demand_mode;			/* Currently unused */
  u8 rem_demand_mode;
  u8 detect_mult;			/* Announced detect_mult, local option */
  u8 rem_detect_mult;			/* Last received detect_mult */

  xxx_time last_tx;			/* Time of last sent periodic control packet */
  xxx_time last_rx;			/* Time of last received valid control packet */

  timer2 *tx_timer;			/* Periodic control packet timer */
  timer2 *hold_timer;			/* Timer for session down detection time */
};



#define BFD_STATE_ADMIN_DOWN	0
#define BFD_STATE_DOWN		1
#define BFD_STATE_INIT		2
#define BFD_STATE_UP		3

#define BFD_DIAG_NOTHING	0
#define BFD_DIAG_TIMEOUT	1
#define BFD_DIAG_ECHO_FAILED	2
#define BFD_DIAG_NEIGHBOR_DOWN	3
#define BFD_DIAG_FWD_RESET	4
#define BFD_DIAG_PATH_DOWN	5
#define BFD_DIAG_C_PATH_DOWN	6
#define BFD_DIAG_ADMIN_DOWN	7
#define BFD_DIAG_RC_PATH_DOWN	8

#define BFD_POLL_TX		1
#define BFD_POLL_RX		2






#endif _BIRD_BFD_H_
