/*
 *	BIRD -- The Border Gateway Protocol
 *
 *	(c) 2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/protocol.h"
#include "nest/route.h"
#include "nest/locks.h"
#include "conf/conf.h"
#include "lib/socket.h"

#include "bgp.h"

static sock *bgp_listen_sk;		/* Global listening socket */
static int bgp_counter;			/* Number of protocol instances using the listening socket */
static list bgp_list;			/* List of active BGP instances */

static void bgp_connect(struct bgp_proto *p);
static void bgp_setup_sk(struct bgp_proto *p, struct bgp_conn *conn, sock *s);

static void
bgp_rt_notify(struct proto *P, net *n, rte *new, rte *old, ea_list *tmpa)
{
}

static struct proto *
bgp_init(struct proto_config *C)
{
  struct bgp_config *c = (struct bgp_config *) C;
  struct proto *P = proto_new(C, sizeof(struct bgp_proto));
  struct bgp_proto *p = (struct bgp_proto *) P;

  P->rt_notify = bgp_rt_notify;
  p->cf = c;
  p->local_as = c->local_as;
  p->remote_as = c->remote_as;
  p->is_internal = (c->local_as == c->remote_as);
  p->conn.state = BS_IDLE;
  p->incoming_conn.state = BS_IDLE;
  p->local_id = C->global->router_id;
  return P;
}

static void
bgp_close(struct bgp_proto *p)
{
  rem_node(&p->bgp_node);
  ASSERT(bgp_counter);
  bgp_counter--;
  if (!bgp_counter)
    {
      rfree(bgp_listen_sk);
      bgp_listen_sk = NULL;
    }
  /* FIXME: Automatic restart after errors? */
}

static void				/* FIXME: Nobody uses */
bgp_reset(struct bgp_proto *p)
{
  bgp_close(p);
  proto_notify_state(&p->p, PS_DOWN);
}

void
bgp_start_timer(timer *t, int value)
{
  /* FIXME: Randomize properly */
  /* FIXME: Check if anybody uses tm_start directly */
  if (value)
    tm_start(t, value);
}

static void
bgp_send_open(struct bgp_conn *conn)
{
  DBG("BGP: Sending open\n");
  conn->sk->rx_hook = bgp_rx;
  tm_stop(conn->connect_retry_timer);
  bgp_schedule_packet(conn, PKT_OPEN);
  conn->state = BS_OPENSENT;
  bgp_start_timer(conn->hold_timer, conn->bgp->cf->initial_hold_time);
}

static void
bgp_connected(sock *sk)
{
  struct bgp_conn *conn = sk->data;

  DBG("BGP: Connected\n");
  bgp_send_open(conn);
}

static void
bgp_connect_timeout(timer *t)
{
  struct bgp_conn *conn = t->data;

  DBG("BGP: Connect timeout, retrying\n");
  bgp_close_conn(conn);
  bgp_connect(conn->bgp);
}

static void
bgp_sock_err(sock *sk, int err)
{
  struct bgp_conn *conn = sk->data;

  DBG("BGP: Socket error %d in state %d\n", err, conn->state);
  switch (conn->state)
    {
    case BS_CONNECT:
    case BS_OPENSENT:
      conn->state = BS_ACTIVE;
      bgp_start_timer(conn->connect_retry_timer, conn->bgp->cf->connect_retry_time);
      break;
    case BS_OPENCONFIRM:
    case BS_ESTABLISHED:
      /* FIXME */
    default:
      bug("bgp_sock_err called in invalid state %d", conn->state);
    }
  bgp_close_conn(conn);
}

static int
bgp_incoming_connection(sock *sk, int dummy)
{
  node *n;

  DBG("BGP: Incoming connection from %I port %d\n", sk->daddr, sk->dport);
  WALK_LIST(n, bgp_list)
    {
      struct bgp_proto *p = SKIP_BACK(struct bgp_proto, bgp_node, n);
      if (ipa_equal(p->cf->remote_ip, sk->daddr) && sk->dport == BGP_PORT)
	{
	  DBG("BGP: Authorized\n");
	  if (p->incoming_conn.sk)
	    {
	      DBG("BGP: But one incoming connection already exists, how is that possible?\n");
	      break;
	    }
	  bgp_setup_sk(p, &p->incoming_conn, sk);
	  bgp_send_open(&p->incoming_conn);
	  return 0;
	}
    }
  DBG("BGP: Unauthorized\n");
  rfree(sk);
  return 0;
}

static void
bgp_hold_timeout(timer *t)
{
  struct bgp_conn *conn = t->data;

  DBG("BGP: Hold timeout, closing connection\n"); /* FIXME: Check states? */
  bgp_error(conn, 4, 0, 0, 0);
}

static void
bgp_keepalive_timeout(timer *t)
{
  struct bgp_conn *conn = t->data;

  DBG("BGP: Keepalive timer\n");
  bgp_schedule_packet(conn, PKT_KEEPALIVE);
}

static void
bgp_setup_sk(struct bgp_proto *p, struct bgp_conn *conn, sock *s)
{
  timer *t;

  s->data = conn;
  s->ttl = p->cf->multihop ? : 1;
  s->rbsize = BGP_RX_BUFFER_SIZE;
  s->tbsize = BGP_TX_BUFFER_SIZE;
  s->tx_hook = bgp_tx;
  s->err_hook = bgp_sock_err;
  s->tos = IP_PREC_INTERNET_CONTROL;

  conn->bgp = p;
  conn->sk = s;
  conn->packets_to_send = 0;

  t = conn->connect_retry_timer = tm_new(p->p.pool);
  t->hook = bgp_connect_timeout;
  t->data = conn;
  t = conn->hold_timer = tm_new(p->p.pool);
  t->hook = bgp_hold_timeout;
  t->data = conn;
  t = conn->keepalive_timer = tm_new(p->p.pool);
  t->hook = bgp_keepalive_timeout;
  t->data = conn;
}

void
bgp_close_conn(struct bgp_conn *conn)
{
  DBG("BGP: Closing connection\n");
  conn->packets_to_send = 0;
  rfree(conn->connect_retry_timer);
  conn->connect_retry_timer = NULL;
  rfree(conn->keepalive_timer);
  conn->keepalive_timer = NULL;
  rfree(conn->hold_timer);
  conn->hold_timer = NULL;
  sk_close(conn->sk);
  conn->sk = NULL;
  conn->state = BS_IDLE;
}

static void
bgp_connect(struct bgp_proto *p)	/* Enter Connect state and start establishing connection */
{
  sock *s;
  struct bgp_conn *conn = &p->conn;

  DBG("BGP: Connecting\n");
  s = sk_new(p->p.pool);
  s->type = SK_TCP_ACTIVE;
  s->saddr = _MI(0x3ea80001);		/* FIXME: Hack */
  s->daddr = p->cf->remote_ip;
#if 0
  s->sport =				/* FIXME */
#endif
  s->dport = BGP_PORT;
  bgp_setup_sk(p, conn, s);
  s->tx_hook = bgp_connected;
  conn->state = BS_CONNECT;
  if (sk_open(s))
    {
      bgp_sock_err(s, 0);
      return;
    }
  DBG("BGP: Waiting for connect success\n");
  bgp_start_timer(conn->connect_retry_timer, p->cf->connect_retry_time);
}

static void
bgp_start_locked(struct object_lock *lock)
{
  struct bgp_proto *p = lock->data;

  DBG("BGP: Got lock\n");
  if (!bgp_counter++)
    init_list(&bgp_list);
  if (!bgp_listen_sk)
    {
      sock *s = sk_new(&root_pool);
      DBG("BGP: Creating incoming socket\n");
      s->type = SK_TCP_PASSIVE;
      s->sport = BGP_PORT;
      s->tos = IP_PREC_INTERNET_CONTROL;
      s->ttl = 1;
      s->rbsize = BGP_RX_BUFFER_SIZE;
      s->rx_hook = bgp_incoming_connection;
      if (sk_open(s))
	{
	  log(L_ERR "Unable to open incoming BGP socket");
	  rfree(s);
	}
      else
	bgp_listen_sk = s;
    }
  add_tail(&bgp_list, &p->bgp_node);
  bgp_connect(p);			/* FIXME: Use neighbor cache for fast up/down transitions? */
}

static int
bgp_start(struct proto *P)
{
  struct bgp_proto *p = (struct bgp_proto *) P;
  struct object_lock *lock;

  /*
   *  Before attempting to create the connection, we need to lock the
   *  port, so that are sure we're the only instance attempting to talk
   *  with that neighbor.
   */

  DBG("BGP: Startup. Acquiring lock.\n");
  lock = p->lock = olock_new(P->pool);
  lock->addr = p->cf->remote_ip;
  lock->type = OBJLOCK_TCP;
  lock->port = BGP_PORT;
  lock->iface = NULL;
  lock->hook = bgp_start_locked;
  lock->data = p;
  olock_acquire(lock);
  return PS_START;
}

static int
bgp_shutdown(struct proto *P)
{
  struct bgp_proto *p = (struct bgp_proto *) P;

  DBG("BGP: Explicit shutdown\n");

  bgp_close(p);
  return PS_DOWN;
}

void
bgp_error(struct bgp_conn *c, unsigned code, unsigned subcode, unsigned data, unsigned len)
{
  DBG("BGP: Error %d,%d,%d,%d\n", code, subcode, data, len); /* FIXME: Better messages */
  if (c->error_flag)
    return;
  c->error_flag = 1;
  c->notify_code = code;
  c->notify_subcode = subcode;
  c->notify_arg = data;
  c->notify_arg_size = len;
  proto_notify_state(&c->bgp->p, PS_STOP);
  bgp_schedule_packet(c, PKT_NOTIFICATION);
}

void
bgp_check(struct bgp_config *c)
{
  if (!c->local_as)
    cf_error("Local AS number must be set");
  if (!c->remote_as)
    cf_error("Neighbor must be configured");
}

struct protocol proto_bgp = {
  name:			"BGP",
  template:		"bgp%d",
  init:			bgp_init,
  start:		bgp_start,
  shutdown:		bgp_shutdown,
#if 0
  dump:			bgp_dump,
  get_status:		bgp_get_status,
  get_route_info:	bgp_get_route_info,
  show_route_data:	bgp_show_route_data
#endif
};
