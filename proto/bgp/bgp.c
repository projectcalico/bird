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
#include "lib/resource.h"

#include "bgp.h"

struct linpool *bgp_linpool;		/* Global temporary pool */
static sock *bgp_listen_sk;		/* Global listening socket */
static int bgp_counter;			/* Number of protocol instances using the listening socket */
static list bgp_list;			/* List of active BGP instances */
static char *bgp_state_names[] = { "Idle", "Connect", "Active", "OpenSent", "OpenConfirm", "Established" };

static void bgp_connect(struct bgp_proto *p);
static void bgp_initiate(struct bgp_proto *p);

void
bgp_close(struct bgp_proto *p)
{
  rem_node(&p->bgp_node);
  ASSERT(bgp_counter);
  bgp_counter--;
  if (!bgp_counter)
    {
      rfree(bgp_listen_sk);
      bgp_listen_sk = NULL;
      rfree(bgp_linpool);
      bgp_linpool = NULL;
    }
}

void
bgp_start_timer(timer *t, int value)
{
  if (value)
    {
      /* The randomization procedure is specified in RFC 1771: 9.2.3.3 */
      t->randomize = value / 4;
      tm_start(t, value - t->randomize);
    }
  else
    tm_stop(t);
}

void
bgp_close_conn(struct bgp_conn *conn)
{
  struct bgp_proto *p = conn->bgp;
  struct bgp_config *cf = p->cf;

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
  if (conn->error_flag > 1)
    {
      if (cf->disable_after_error)
	p->p.disabled = 1;
      if (p->last_connect && (bird_clock_t)(p->last_connect + cf->error_amnesia_time) < now)
	p->startup_delay = 0;
      if (!p->startup_delay)
	p->startup_delay = cf->error_delay_time_min;
      else
	{
	  p->startup_delay *= 2;
	  if (p->startup_delay > cf->error_delay_time_max)
	    p->startup_delay = cf->error_delay_time_max;
	}
    }
  if (conn->primary)
    {
      bgp_close(p);
      p->conn = NULL;
      proto_notify_state(&p->p, PS_DOWN);
    }
  else if (conn->error_flag > 1)
    bgp_initiate(p);
}

static int
bgp_graceful_close_conn(struct bgp_conn *c)
{
  switch (c->state)
    {
    case BS_IDLE:
      return 0;
    case BS_CONNECT:
    case BS_ACTIVE:
      bgp_close_conn(c);
      return 1;
    case BS_OPENSENT:
    case BS_OPENCONFIRM:
    case BS_ESTABLISHED:
      bgp_error(c, 6, 0, NULL, 0);
      return 1;
    default:
      bug("bgp_graceful_close_conn: Unknown state %d", c->state);
    }
}

static void
bgp_send_open(struct bgp_conn *conn)
{
  DBG("BGP: Sending open\n");
  conn->sk->rx_hook = bgp_rx;
  conn->sk->tx_hook = bgp_tx;
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
      break;
    default:
      bug("bgp_sock_err called in invalid state %d", conn->state);
    }
  bgp_close_conn(conn);
}

static void
bgp_hold_timeout(timer *t)
{
  struct bgp_conn *conn = t->data;

  DBG("BGP: Hold timeout, closing connection\n");
  bgp_error(conn, 4, 0, NULL, 0);
}

static void
bgp_keepalive_timeout(timer *t)
{
  struct bgp_conn *conn = t->data;

  DBG("BGP: Keepalive timer\n");
  bgp_schedule_packet(conn, PKT_KEEPALIVE);
}

static void
bgp_setup_conn(struct bgp_proto *p, struct bgp_conn *conn)
{
  timer *t;

  conn->sk = NULL;
  conn->bgp = p;
  conn->packets_to_send = 0;
  conn->error_flag = 0;
  conn->primary = 0;

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

static void
bgp_setup_sk(struct bgp_proto *p, struct bgp_conn *conn, sock *s)
{
  s->data = conn;
  s->ttl = p->cf->multihop ? : 1;
  s->rbsize = BGP_RX_BUFFER_SIZE;
  s->tbsize = BGP_TX_BUFFER_SIZE;
  s->err_hook = bgp_sock_err;
  s->tos = IP_PREC_INTERNET_CONTROL;
  conn->sk = s;
}

static void
bgp_connect(struct bgp_proto *p)	/* Enter Connect state and start establishing connection */
{
  sock *s;
  struct bgp_conn *conn = &p->outgoing_conn;

  DBG("BGP: Connecting\n");
  p->last_connect = now;
  s = sk_new(p->p.pool);
  s->type = SK_TCP_ACTIVE;
  if (ipa_nonzero(p->cf->source_addr))
    s->saddr = p->cf->source_addr;
  else
    s->saddr = p->local_addr;
  s->daddr = p->cf->remote_ip;
  s->dport = BGP_PORT;
  bgp_setup_conn(p, conn);
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
bgp_initiate(struct bgp_proto *p)
{
  unsigned delay;

  delay = p->cf->start_delay_time;
  if (p->startup_delay > delay)
    delay = p->startup_delay;
  if (delay)
    {
      DBG("BGP: Connect delayed by %d seconds\n", delay);
      bgp_setup_conn(p, &p->outgoing_conn);
      bgp_start_timer(p->outgoing_conn.connect_retry_timer, delay);
    }
  else
    bgp_connect(p);
}

static int
bgp_incoming_connection(sock *sk, int dummy)
{
  node *n;

  DBG("BGP: Incoming connection from %I port %d\n", sk->daddr, sk->dport);
  WALK_LIST(n, bgp_list)
    {
      struct bgp_proto *p = SKIP_BACK(struct bgp_proto, bgp_node, n);
      if (ipa_equal(p->cf->remote_ip, sk->daddr))
	{
	  DBG("BGP: Authorized\n");
	  if (p->incoming_conn.sk)
	    {
	      DBG("BGP: But one incoming connection already exists, how is that possible?\n");
	      break;
	    }
	  bgp_setup_conn(p, &p->incoming_conn);
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
bgp_start_neighbor(struct bgp_proto *p)
{
  p->local_addr = p->neigh->iface->addr->ip;
  DBG("BGP: local=%I remote=%I\n", p->local_addr, p->next_hop);
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
  if (!bgp_linpool)
    bgp_linpool = lp_new(&root_pool, 4080);
  add_tail(&bgp_list, &p->bgp_node);
  bgp_initiate(p);
}

static void
bgp_neigh_notify(neighbor *n)
{
  struct bgp_proto *p = (struct bgp_proto *) n->proto;

  if (n->iface)
    {
      DBG("BGP: Neighbor is reachable\n");
      bgp_start_neighbor(p);
    }
  else
    {
      DBG("BGP: Neighbor is unreachable\n");
      /* Send cease packets, but don't wait for them to be delivered */
      bgp_graceful_close_conn(&p->outgoing_conn);
      bgp_graceful_close_conn(&p->incoming_conn);
      proto_notify_state(&p->p, PS_DOWN);
    }
}

static void
bgp_start_locked(struct object_lock *lock)
{
  struct bgp_proto *p = lock->data;
  struct bgp_config *cf = p->cf;

  DBG("BGP: Got lock\n");
  p->local_id = cf->c.global->router_id;
  p->next_hop = cf->multihop ? cf->multihop_via : cf->remote_ip;
  p->neigh = neigh_find(&p->p, &p->next_hop, NEF_STICKY);
  if (!p->neigh)
    {
      log(L_ERR "%s: Invalid next hop %I", p->p.name, p->next_hop);
      p->p.disabled = 1;
      proto_notify_state(&p->p, PS_DOWN);
    }
  else if (p->neigh->iface)
    bgp_start_neighbor(p);
  else
    DBG("BGP: Waiting for neighbor\n");
}

static int
bgp_start(struct proto *P)
{
  struct bgp_proto *p = (struct bgp_proto *) P;
  struct object_lock *lock;

  DBG("BGP: Startup.\n");
  p->outgoing_conn.state = BS_IDLE;
  p->incoming_conn.state = BS_IDLE;
  p->startup_delay = 0;

  /*
   *  Before attempting to create the connection, we need to lock the
   *  port, so that are sure we're the only instance attempting to talk
   *  with that neighbor.
   */

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

  /*
   *  We want to send the Cease notification message to all connections
   *  we have open, but we don't want to wait for all of them to complete.
   *  We are willing to handle the primary connection carefully, but for
   *  the others we just try to send the packet and if there is no buffer
   *  space free, we'll gracefully finish.
   */

  proto_notify_state(&p->p, PS_STOP);
  if (!p->conn)
    {
      if (p->outgoing_conn.state != BS_IDLE)
	p->outgoing_conn.primary = 1;	/* Shuts protocol down after connection close */
      else if (p->incoming_conn.state != BS_IDLE)
	p->incoming_conn.primary = 1;
    }
  if (bgp_graceful_close_conn(&p->outgoing_conn) || bgp_graceful_close_conn(&p->incoming_conn))
    return p->p.proto_state;
  else
    {
      /* No connections open, shutdown automatically */
      bgp_close(p);
      return PS_DOWN;
    }
}

static struct proto *
bgp_init(struct proto_config *C)
{
  struct bgp_config *c = (struct bgp_config *) C;
  struct proto *P = proto_new(C, sizeof(struct bgp_proto));
  struct bgp_proto *p = (struct bgp_proto *) P;

  P->rt_notify = bgp_rt_notify;
  P->rte_better = bgp_rte_better;
  P->import_control = bgp_import_control;
  P->neigh_notify = bgp_neigh_notify;
  p->cf = c;
  p->local_as = c->local_as;
  p->remote_as = c->remote_as;
  p->is_internal = (c->local_as == c->remote_as);
  return P;
}

void
bgp_error(struct bgp_conn *c, unsigned code, unsigned subcode, byte *data, int len)
{
  if (c->error_flag)
    return;
  bgp_log_error(c->bgp, "Error", code, subcode, data, (len > 0) ? len : -len);
  c->error_flag = 1 + (code != 6);
  c->notify_code = code;
  c->notify_subcode = subcode;
  c->notify_data = data;
  c->notify_size = (len > 0) ? len : 0;
  if (c->primary)
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

static void
bgp_get_status(struct proto *P, byte *buf)
{
  struct bgp_proto *p = (struct bgp_proto *) P;

  strcpy(buf, bgp_state_names[MAX(p->incoming_conn.state, p->outgoing_conn.state)]);
}

static int
bgp_reconfigure(struct proto *P, struct proto_config *C)
{
  struct bgp_config *new = (struct bgp_config *) C;
  struct bgp_proto *p = (struct bgp_proto *) P;
  struct bgp_config *old = p->cf;

  return !memcmp(((byte *) old) + sizeof(struct proto_config),
		 ((byte *) new) + sizeof(struct proto_config),
		 sizeof(struct bgp_config) - sizeof(struct proto_config));
}

struct protocol proto_bgp = {
  name:			"BGP",
  template:		"bgp%d",
  attr_class:		EAP_BGP,
  init:			bgp_init,
  start:		bgp_start,
  shutdown:		bgp_shutdown,
  get_status:		bgp_get_status,
  get_attr:		bgp_get_attr,
  reconfigure:		bgp_reconfigure,
#if 0
  dump:			bgp_dump,
  get_route_info:	bgp_get_route_info,
#endif
};
