#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/protocol.h"
#include "nest/route.h"
#include "nest/cli.h"
#include "conf/conf.h"
#include "lib/socket.h"
#include "lib/resource.h"
#include "lib/string.h"

#include "bfd.h"


#define HASH_ID_KEY	loc_id
#define HASH_ID_NEXT	next_id
#define HASH_ID_EQ(a,b)	((a)==(b))
#define HASH_ID_FN(a)	(a)

#define HASH_IP_KEY	addr
#define HASH_IP_NEXT	next_ip
#define HASH_IP_EQ(a,b)	((a)==(b))
#define HASH_IP_FN(a)	(a == b)

static u32
bfd_get_free_id(struct bfd_proto *p)
{
  u32 id;
  for (id = random_u32(); 1; id++)
    if (id && !bfd_find_session_by_id(p, id))
      break;

  return id;
}

static void
bfd_add_session(struct bfd_proto *p, ip_addr addr, struct bfd_session_config *opts)
{
  birdloop_enter(p->loop);

  struct bfd_session *s = sl_alloc(p->session_slab);
  bzero(s, sizeof(struct bfd_session));

  /* Initialization of state variables - see RFC 5880 3.8.1 */
  s->loc_state = BFD_STATE_DOWN;
  s->rem_state = BFD_STATE_DOWN;
  s->loc_id = bfd_get_free_id(p);
  s->des_min_tx_int = s->des_min_tx_new = s->opts->idle_tx_int;
  s->req_min_rx_int = s->req_min_rx_new = s->opts->min_rx_int;
  s->detect_mult = s->opts->multiplier;
  s->rem_min_rx_int = 1;

  HASH_INSERT(p->session_hash_id, HASH_ID, s);
  HASH_INSERT(p->session_hash_ip, HASH_IP, s);

  s->tx_timer = tm2_new_set(xxx, bfd_rx_timer_hook, s, 0, 0);
  s->hold_timer = tm2_new_set(xxx, bfd_hold_timer_hook, s, 0, 0);
  bfd_session_update_tx_interval(s);

  birdloop_leave(p->loop);
}

static void
bfd_open_session(struct bfd_proto *p, struct bfd_session *s, ip_addr local, struct iface *ifa)
{
  birdloop_enter(p->loop);

  s->bsock = bfd_get_socket(p, local, ifa);
  s->local = local;
  s->iface = ifa;
  s->opened = 1;

  bfd_session_control_tx_timer(s);

  birdloop_leave(p->loop);
}

static void
bfd_close_session(struct bfd_proto *p, struct bfd_session *s)
{
  birdloop_enter(p->loop);

  bfd_free_socket(s->bsock);
  s->bsock = NULL;
  s->local = IPA_NONE;
  s->iface = NULL;
  s->opened = 0;

  bfd_session_control_tx_timer(s);

  birdloop_leave(p->loop);
}

static void
bfd_remove_session(struct bfd_proto *p, struct bfd_session *s)
{
  birdloop_enter(p->loop);

  bfd_free_socket(s->bsock);

  rfree(s->tx_timer);
  rfree(s->hold_timer);

  HASH_REMOVE(p->session_hash_id, HASH_ID, s);
  HASH_REMOVE(p->session_hash_ip, HASH_IP, s);

  sl_free(p->session_slab, s);

  birdloop_leave(p->loop);
}

struct bfd_session *
bfd_find_session_by_id(struct bfd_proto *p, u32 id)
{
  return HASH_FIND(p->session_hash_id, HASH_ID, id);
}

struct bfd_session *
bfd_find_session_by_addr(struct bfd_proto *p, ip_addr addr)
{
  return HASH_FIND(p->session_hash_ip, HASH_IP, addr);
}

static void
bfd_rx_timer_hook(timer2 *t)
{
  struct bfd_session *s = timer->data;

  s->last_tx = xxx_now;
  bfd_send_ctl(s->bfd, s, 0);
}

static void
bfd_hold_timer_hook(timer2 *t)
{
  bfd_session_timeout(timer->data);
}

static void 
bfd_session_timeout(struct bfd_session *s)
{
  s->rem_state = BFD_STATE_DOWN;
  s->rem_id = 0;
  s->rem_min_tx_int = 0;
  s->rem_min_rx_int = 1;
  s->rem_demand_mode = 0;

  bfd_session_update_state(s, BFD_STATE_DOWN, BFD_DIAG_TIMEOUT);
}

static void
bfd_session_control_tx_timer(struct bfd_session *s)
{
  if (!s->opened)
    goto stop;

  if (s->passive && (s->rem_id == 0))
    goto stop;

  if (s->rem_demand_mode && 
      !s->poll_active && 
      (s->loc_state == BFD_STATE_UP) &&
      (s->rem_state == BFD_STATE_UP))
    goto stop;

  if (s->rem_min_rx_int == 0)
    goto stop;

  /* So TX timer should run */
  if (tm2_active(s->tx_timer))
    return;

  tm2_start(s->tx_timer, 0);
  return;

 stop:
  tm2_stop(s->tx_timer);
  s->last_tx = 0;
}

static void
bfd_session_update_tx_interval(struct bfd_session *s)
{
  u32 tx_int = MAX(s->des_min_tx_int, s->rem_min_rx_int);
  u32 tx_int_l = tx_int - (tx_int / 4);	 // 75 %
  u32 tx_int_h = tx_int - (tx_int / 10); // 90 %

  s->tx_timer->recurrent = tx_int_l;
  s->tx_timer->randomize = tx_int_h - tx_int_l;

  /* Do not set timer if no previous event */
  if (!s->last_tx)
    return;

  /* Set timer relative to last tx_timer event */
  tm2_set(s->tx_timer, s->last_tx + tx_int_l);
}

static void
bfd_session_update_detection_time(struct bfd_session *s, int kick)
{
  xxx_time timeout = (xxx_time) MAX(s->req_min_rx_int, s->rem_min_tx_int) * s->rem_detect_mult;

  if (kick)
    s->last_rx = xxx_now;

  if (!s->last_rx)
    return;

  tm2_set(s->hold_timer, s->last_rx + timeout);
}

void
bfd_session_request_poll(struct bfd_session *s, u8 request)
{
  s->poll_scheduled |= request;

  if (s->poll_active)
    return;

  s->poll_active = s->poll_scheduled;
  s->poll_scheduled = 0;
  bfd_send_ctl(s->bfd, s, 0);
}

void
bfd_session_terminate_poll(struct bfd_session *s)
{
  u8 poll_done = s->poll_active & ~s->poll_scheduled;

  if (poll_done & BFD_POLL_TX)
    s->des_min_tx_int = s->des_min_tx_new;

  if (poll_done & BFD_POLL_RX)
    s->req_min_rx_int = s->req_min_rx_new;

  s->poll_active = 0;

  /* Timers are updated by caller - bfd_session_process_ctl() */

  xxx_restart_poll();
}

void
bfd_session_process_ctl(struct bfd_session *s, u8 flags, u32 old_rx_int, u32 old_tx_int)
{
  if (s->poll_active && (flags & BFD_FLAG_FINAL))
    bfd_session_terminate_poll(s);

  if ((s->des_min_tx_int != old_rx_int) || (s->rem_min_rx_int != old_tx_int))
    bfd_session_update_tx_interval(s);

  bfd_session_update_detection_time(s, 1);

  /* Update session state */
  int next_state = 0;
  int diag = BFD_DIAG_NOTHING;

  switch (s->loc_state)
  {
  case BFD_STATE_ADMIN_DOWN:
    return;

  case BFD_STATE_DOWN:
    if (s->rem_state == BFD_STATE_DOWN)		next_state = BFD_STATE_INIT;
    else if (s->rem_state == BFD_STATE_INIT)	next_state = BFD_STATE_UP;
    break;

  case BFD_STATE_INIT:
    if (s->rem_state == BFD_STATE_ADMIN_DOWN)	next_state = BFD_STATE_DOWN, diag = BFD_DIAG_NEIGHBOR_DOWN;
    else if (s->rem_state >= BFD_STATE_INIT)	next_state = BFD_STATE_UP;
    break;

  case BFD_STATE_UP:
    if (s->rem_state <= BFD_STATE_DOWN)		next_state = BFD_STATE_DOWN, diag = BFD_DIAG_NEIGHBOR_DOWN;
    break;
  }

  if (next_state)
    bfd_session_update_state(s, next_state, diag);

  bfd_session_control_tx_timer(s);

  if (flags & BFD_FLAG_POLL)
    bfd_send_ctl(p, s, 1);
}


static void
bfd_session_set_min_tx(struct bfd_session *s, u32 val)
{
  /* Note that des_min_tx_int <= des_min_tx_new */

  if (val == s->des_min_tx_new)
    return;

  s->des_min_tx_new = val;

  /* Postpone timer update if des_min_tx_int increases and the session is up */
  if ((s->loc_state != BFD_STATE_UP) || (val < s->des_min_tx_int))
  {
    s->des_min_tx_int = val;
    bfd_session_update_tx_interval(s);
  }

  bfd_session_request_poll(s, BFD_POLL_TX);
}

static void
bfd_session_set_min_rx(struct bfd_session *s, u32 val)
{
  /* Note that req_min_rx_int >= req_min_rx_new */

  if (val == s->req_min_rx_new)
    return;

  s->req_min_rx_new = val; 

  /* Postpone timer update if req_min_rx_int decreases and the session is up */
  if ((s->loc_state != BFD_STATE_UP) || (val > s->req_min_rx_int))
  {
    s->req_min_rx_int = val;
    bfd_session_update_detection_time(s, 0);
  }

  bfd_session_request_poll(s, BFD_POLL_RX);
}

static void
bfd_start_neighbor(struct bfd_proto *p, struct bfd_neighbor *n)
{
  n->session = bfd_add_session(p, n->addr, n->opts);

  if (n->opts->multihop)
  {
    bfd_open_session(p, n->session, n->local, NULL);
    return;
  }

  struct neighbor *nb = neigh_find2(&p->p, &n->addr, n->iface, NEF_STICKY);
  if (!nb)
  {
    log(L_ERR "%s: Invalid remote address %I%J", p->p.name, n->addr, n->iface);
    return;
  }

  if (nb->data)
  {
    log(L_ERR "%s: Duplicate remote address %I", p->p.name, n->addr);
    return;
  }

  nb->data = n->session;

  if (nb->scope > 0)
    bfd_open_session(p, n->session, nb->iface->addr->ip, nb->iface);
  else
    TRACE(D_EVENTS, "Waiting for %I%J to become my neighbor", n->addr, cf->iface);
}

static void
bfd_stop_neighbor(struct bfd_proto *p, struct bfd_neighbor *n)
{
  if (!n->opts->multihop)
  {
    struct neighbor *nb = neigh_find2(&p->p, &n->addr, n->iface, 0);
    if (nb)
      nb->data = NULL;
  }

  bfd_remove_session(p, n->session);
}


static void
bfd_neigh_notify(struct neighbor *nb)
{
  struct bfd_proto *p = (struct bfd_proto *) nb->proto;
  struct bfd_session *s = nb->data;

  if (!s)
    return;

  if ((nb->scope > 0) && !s->opened)
    bfd_open_session(p, s, nb->iface->addr->ip, nb->iface);

  if ((nb->scope <= 0) && s->opened)
    bfd_close_session(p, s);
}



static struct proto *
bfd_init(struct proto_config *c)
{
  struct proto *p = proto_new(c, sizeof(struct bfd_proto));

  p->if_notify = bfd_if_notify;
  p->ifa_notify = bfd_ifa_notify;

  return p;
}

static int
bfd_start(struct proto *P)
{
  struct bfd_proto *p = (struct bfd_proto *) P;
  struct bfd_config *cf = (struct bfd_config *) (P->cf);

  p->session_slab = sl_new(P->pool, sizeof(struct bfd_session));
  init_list(&p->sockets);

  HASH_INIT(p->session_hash_id, P->pool, 16);
  HASH_INIT(p->session_hash_ip, P->pool, 16);

  struct bfd_neighbor *n;
  WALK_LIST(n, cf->neighbors)
    bfd_start_neighbor(p, n);

  return PS_UP;
}


static int
bfd_shutdown(struct proto *P)
{
  struct bfd_proto *p = (struct bfd_proto *) P;

  return PS_DOWN;
}

static inline int
bfd_same_neighbor(struct bfd_neighbor *x, struct bfd_neighbor *y)
{
  return ipa_equal(x->addr, y->addr) && ipa_equal(x->local, y->local) &&
    (x->iface == y->iface) && (x->opts->multihop == y->opts->multihop);
}

static void
bfd_match_neighbor(struct bfd_proto *p, struct bfd_neighbor *on, struct bfd_config *new)
{
  struct bfd_neighbor *nn;

  if (r->neigh)
    r->neigh->data = NULL;

  WALK_LIST(nn, new->neighbors)
    if (bfd_same_neighbor(nn, on))
    {
      nn->session = on->session;
      // XXX reconfiguration of session?
      return;
    }

  bfd_stop_neighbor(p, on);
}

static int
bfd_reconfigure(struct proto *P, struct proto_config *c)
{
  struct bfd_proto *p = (struct bfd_proto *) P;
  struct bfd_config *old = (struct bfd_config *) (P->cf);
  struct bfd_config *new = (struct bfd_config *) c;
  struct bfd_neighbor *n;

  WALK_LIST(n, old->neighbors)
    bfd_match_neighbor(p, n, new);

  WALK_LIST(n, new->neighbors)
    if (!n->session)
      bfd_start_neighbor(p, n);

  return 1;
}

static void
bfd_copy_config(struct proto_config *dest, struct proto_config *src)
{
  struct bfd_config *d = (struct bfd_config *) dest;
  struct bfd_config *s = (struct bfd_config *) src;

  /* We clean up patt_list, ifaces are non-sharable */
  init_list(&d->patt_list);

  /* We copy pref_list, shallow copy suffices */
  cfg_copy_list(&d->pref_list, &s->pref_list, sizeof(struct bfd_prefix_config));
}

struct protocol proto_bfd = {
  .name =		"BFD",
  .template =		"bfd%d",
  .init =		bfd_init,
  .start =		bfd_start,
  .shutdown =		bfd_shutdown,
  .reconfigure =	bfd_reconfigure,
  .copy_config =	bfd_copy_config,
};
