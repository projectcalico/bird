/*
 *	BIRD Internet Routing Daemon -- Unix I/O
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "nest/bird.h"
#include "lib/lists.h"
#include "lib/resource.h"
#include "lib/timer.h"
#include "lib/socket.h"
#include "nest/iface.h"

#include "lib/unix.h"

/*
 *	Timers
 */

#define NEAR_TIMER_LIMIT 4

#ifdef TIME_T_IS_64BIT
#define TIME_INFINITY 0x7fffffffffffffff
#else
#ifdef TIME_T_IS_SIGNED
#define TIME_INFINITY 0x7fffffff
#else
#define TIME_INFINITY 0xffffffff
#endif
#endif

static list near_timers, far_timers;
static bird_clock_t first_far_timer = TIME_INFINITY;

bird_clock_t now;

static void
tm_free(resource *r)
{
  timer *t = (timer *) r;

  tm_stop(t);
}

static void
tm_dump(resource *r)
{
  timer *t = (timer *) r;

  debug("(code %p, data %p, ");
  if (t->randomize)
    debug("rand %d, ", t->randomize);
  if (t->recurrent)
    debug("recur %d, ", t->recurrent);
  if (t->expires)
    debug("expires in %d sec)\n", t->expires - now);
  else
    debug("inactive)\n");
}

static struct resclass tm_class = {
  "Timer",
  sizeof(timer),
  tm_free,
  tm_dump
};

timer *
tm_new(pool *p)
{
  timer *t = ralloc(p, &tm_class);
  t->hook = NULL;
  t->data = NULL;
  t->randomize = 0;
  t->expires = 0;
  return t;
}

static inline void
tm_insert_near(timer *t)
{
  node *n = HEAD(near_timers);

  while (n->next && (SKIP_BACK(timer, n, n)->expires < t->expires))
    n = n->next;
  insert_node(&t->n, n->prev);
}

void
tm_start(timer *t, unsigned after)
{
  bird_clock_t when;

  if (t->randomize)
    after += random() % (t->randomize + 1);
  when = now + after;
  if (t->expires == when)
    return;
  if (t->expires)
    rem_node(&t->n);
  t->expires = when;
  if (after <= NEAR_TIMER_LIMIT)
    tm_insert_near(t);
  else
    {
      if (!first_far_timer || first_far_timer > when)
	first_far_timer = when;
      add_tail(&far_timers, &t->n);
    }
}

void
tm_stop(timer *t)
{
  if (t->expires)
    {
      rem_node(&t->n);
      t->expires = 0;
    }
}

static void
tm_dump_them(char *name, list *l)
{
  node *n;
  timer *t;

  debug("%s timers:\n", name);
  WALK_LIST(n, *l)
    {
      t = SKIP_BACK(timer, n, n);
      debug("%p ", t);
      tm_dump(&t->r);
    }
  debug("\n");
}

void
tm_dump_all(void)
{
  tm_dump_them("Near", &near_timers);
  tm_dump_them("Far", &far_timers);
}

static inline time_t
tm_first_shot(void)
{
  time_t x = first_far_timer;

  if (!EMPTY_LIST(near_timers))
    {
      timer *t = SKIP_BACK(timer, n, HEAD(near_timers));
      if (t->expires < x)
	x = t->expires;
    }
  return x;
}

static void
tm_shot(void)
{
  timer *t;
  node *n, *m;

  if (first_far_timer <= now)
    {
      clock_t limit = now + NEAR_TIMER_LIMIT;
      first_far_timer = TIME_INFINITY;
      n = HEAD(far_timers);
      while (m = n->next)
	{
	  t = SKIP_BACK(timer, n, n);
	  if (t->expires <= limit)
	    {
	      rem_node(n);
	      tm_insert_near(t);
	    }
	  else if (t->expires < first_far_timer)
	    first_far_timer = t->expires;
	  n = m;
	}
    }
  while ((n = HEAD(near_timers)) -> next)
    {
      int delay;
      t = SKIP_BACK(timer, n, n);
      if (t->expires > now)
	break;
      rem_node(n);
      delay = t->expires - now;
      t->expires = 0;
      if (t->recurrent)
	{
	  int i = t->recurrent - delay;
	  if (i < 0)
	    i = 0;
	  tm_start(t, i);
	}
      t->hook(t);
    }
}

/*
 *	Sockets
 */

static list sock_list;

static void
sk_free(resource *r)
{
  sock *s = (sock *) r;

  if (s->fd >= 0)
    rem_node(&s->n);
}

static void
sk_dump(resource *r)
{
  sock *s = (sock *) r;
  static char *sk_type_names[] = { "TCP<", "TCP>", "TCP", "UDP", "UDP/MC", "IP", "IP/MC" };

  debug("(%s, ud=%p, sa=%08x, sp=%d, da=%08x, dp=%d, tos=%d, ttl=%d, if=%s)\n",
	sk_type_names[s->type],
	s->data,
	s->saddr,
	s->sport,
	s->daddr,
	s->dport,
	s->tos,
	s->ttl,
	s->iface ? s->iface->name : "none");
}

static struct resclass sk_class = {
  "Socket",
  sizeof(sock),
  sk_free,
  sk_dump
};

sock *
sk_new(pool *p)
{
  sock *s = ralloc(p, &sk_class);
  s->pool = p;
  s->data = NULL;
  s->saddr = s->daddr = IPA_NONE;
  s->sport = s->dport = 0;
  s->tos = s->ttl = -1;
  s->iface = NULL;
  s->rbuf = NULL;
  s->rx_hook = NULL;
  s->rbsize = 0;
  s->tbuf = NULL;
  s->tx_hook = NULL;
  s->tbsize = 0;
  s->err_hook = NULL;
  s->fd = -1;
  return s;
}

#define ERR(x) do { err = x; goto bad; } while(0)

static inline void
set_inaddr(struct in_addr *ia, ip_addr a)
{
  a = ipa_hton(a);
  memcpy(&ia->s_addr, &a, sizeof(a));
}

static void
fill_in_sockaddr(struct sockaddr_in *sa, ip_addr a, unsigned port)
{
  sa->sin_family = AF_INET;
  sa->sin_port = htons(port);
  set_inaddr(&sa->sin_addr, a);
}

void
get_sockaddr(struct sockaddr_in *sa, ip_addr *a, unsigned *port)
{
  if (sa->sin_family != AF_INET)
    die("get_sockaddr called for wrong address family");
  if (port)
    *port = ntohs(sa->sin_port);
  memcpy(a, &sa->sin_addr.s_addr, sizeof(*a));
  *a = ipa_ntoh(*a);
}

static char *
sk_setup(sock *s)
{
  int fd = s->fd;
  int one = 1;
  char *err;

  if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
    ERR("fcntl(O_NONBLOCK)");
  if ((s->tos >= 0) && setsockopt(fd, SOL_IP, IP_TOS, &s->tos, sizeof(s->tos)) < 0)
    ERR("IP_TOS");
  if (s->ttl >= 0)
    {
      if (setsockopt(fd, SOL_IP, IP_TTL, &s->ttl, sizeof(s->ttl)) < 0)
	ERR("IP_TTL");
      if (setsockopt(fd, SOL_SOCKET, SO_DONTROUTE, &one, sizeof(one)) < 0)
	ERR("SO_DONTROUTE");
    }
#ifdef IP_PMTUDISC
  if (s->type != SK_TCP_PASSIVE && s->type != SK_TCP_ACTIVE)
    {
      int dont = IP_PMTUDISC_DONT;
      if (setsockopt(fd, SOL_IP, IP_PMTUDISC, &dont, sizeof(dont)) < 0)
	ERR("IP_PMTUDISC");
    }
#endif
  /* FIXME: Set send/receive buffers? */
  /* FIXME: Set keepalive for TCP connections? */
  err = NULL;
bad:
  return err;
}

static void
sk_alloc_bufs(sock *s)
{
  if (!s->rbuf && s->rbsize)
    s->rbuf = mb_alloc(s->pool, s->rbsize);
  s->rpos = s->rbuf;
  if (!s->tbuf && s->tbsize)
    s->tbuf = mb_alloc(s->pool, s->tbsize);
  s->tpos = s->ttx = s->tbuf;
}

void
sk_tcp_connected(sock *s)
{
  s->rx_hook(s, 0);
  s->type = SK_TCP;
  sk_alloc_bufs(s);
}

int
sk_open(sock *s)
{
  int fd, e;
  struct sockaddr_in sa;
  int zero = 0;
  int one = 1;
  int type = s->type;
  int has_src = ipa_nonzero(s->saddr) || s->sport;
  int has_dest = ipa_nonzero(s->daddr);
  char *err;

  switch (type)
    {
    case SK_TCP_ACTIVE:
    case SK_TCP_PASSIVE:
      fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
      break;
    case SK_UDP:
    case SK_UDP_MC:
      fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
      break;
    case SK_IP:
    case SK_IP_MC:
      fd = socket(PF_INET, SOCK_RAW, s->dport);
      break;
    default:
      die("sk_open() called for invalid sock type %d", s->type);
    }
  if (fd < 0)
    die("sk_open: socket: %m");
  s->fd = fd;

  if (err = sk_setup(s))
    goto bad;
  switch (type)
    {
    case SK_UDP:
    case SK_IP:
      if (s->iface)			/* It's a broadcast socket */
	if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one)) < 0)
	  ERR("SO_BROADCAST");
      break;
    case SK_UDP_MC:
    case SK_IP_MC:
      {
#ifdef HAVE_IP_MREQN
	struct ip_mreqn mreq;
#define mreq_add mreq
	mreq.imr_ifindex = s->iface->index;
	if (has_src)
	  set_inaddr(&mreq.imr_address, s->saddr);
	else
	  set_inaddr(&mreq.imr_address, s->iface->ifa->ip);
#else
	struct in_addr mreq;
	struct ip_mreq mreq_add;
	if (has_src)
	  set_inaddr(&mreq, s->saddr);
	else
	  set_inaddr(&mreq, s->iface->ip);
	memcpy(&mreq_add.imr_interface, &mreq, sizeof(struct in_addr));
#endif
	set_inaddr(&mreq_add.imr_multiaddr, s->daddr);
	if (has_dest)
	  {
	    if (
#ifdef IP_DEFAULT_MULTICAST_TTL
		s->ttl != IP_DEFAULT_MULTICAST_TTL &&
#endif
		setsockopt(fd, SOL_IP, IP_MULTICAST_TTL, &s->ttl, sizeof(s->ttl)) < 0)
	      ERR("IP_MULTICAST_TTL");
	    if (
#ifdef IP_DEFAULT_MULTICAST_LOOP
		IP_DEFAULT_MULTICAST_LOOP &&
#endif
		setsockopt(fd, SOL_IP, IP_MULTICAST_LOOP, &zero, sizeof(zero)) < 0)
	      ERR("IP_MULTICAST_LOOP");
	    if (setsockopt(fd, SOL_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq)) < 0)
	      ERR("IP_MULTICAST_IF");
	}
      if (has_src && setsockopt(fd, SOL_IP, IP_ADD_MEMBERSHIP, &mreq_add, sizeof(mreq_add)) < 0)
	ERR("IP_ADD_MEMBERSHIP");
      break;
      }
    }
  if (has_src)
    {
      int port;

      if (type == SK_IP || type == SK_IP_MC)
	port = 0;
      else
	{
	  port = s->sport;
	  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
	    ERR("SO_REUSEADDR");
	}
      fill_in_sockaddr(&sa, s->saddr, port);
      if (bind(fd, (struct sockaddr *) &sa, sizeof(sa)) < 0)
	ERR("bind");
    }
  fill_in_sockaddr(&sa, s->daddr, s->dport);
  switch (type)
    {
    case SK_TCP_ACTIVE:
      if (connect(fd, (struct sockaddr *) &sa, sizeof(sa)) >= 0)
	sk_tcp_connected(s);
      else if (errno != EINTR && errno != EAGAIN)
	ERR("connect");
      break;
    case SK_TCP_PASSIVE:
      if (listen(fd, 8))
	ERR("listen");
      break;
    }

  sk_alloc_bufs(s);
  add_tail(&sock_list, &s->n);
  return 0;

bad:
  log(L_ERR "sk_open: %s: %m", err);
  close(fd);
  s->fd = -1;
  return -1;
}

static int
sk_maybe_write(sock *s)
{
  int e;

  switch (s->type)
    {
    case SK_TCP:
      while (s->ttx != s->tpos)
	{
	  e = write(s->fd, s->ttx, s->tpos - s->ttx);
	  if (e < 0)
	    {
	      if (errno != EINTR && errno != EAGAIN)
		{
		  log(L_ERR "write: %m");
		  s->err_hook(s, errno);
		  return -1;
		}
	      return 0;
	    }
	  s->ttx += e;
	}
      s->ttx = s->tpos = s->tbuf;
      return 1;
    case SK_UDP:
    case SK_UDP_MC:
    case SK_IP:
    case SK_IP_MC:
      {
	struct sockaddr_in sa;

	if (s->tbuf == s->tpos)
	  return 1;
	fill_in_sockaddr(&sa, s->faddr, s->fport);
	e = sendto(s->fd, s->tbuf, s->tpos - s->tbuf, 0, (struct sockaddr *) &sa, sizeof(sa));
	if (e < 0)
	  {
	    if (errno != EINTR && errno != EAGAIN)
	      {
		log(L_ERR "sendto: %m");
		s->err_hook(s, errno);
		return -1;
	      }
	    return 0;
	  }
	s->tpos = s->tbuf;
	return 1;
      }
    default:
      die("sk_maybe_write: unknown socket type %d", s->type);
    }
}

int
sk_send(sock *s, unsigned len)
{
  s->faddr = s->daddr;
  s->fport = s->dport;
  s->ttx = s->tbuf;
  s->tpos = s->tbuf + len;
  return sk_maybe_write(s);
}

int
sk_send_to(sock *s, unsigned len, ip_addr addr, unsigned port)
{
  s->faddr = addr;
  s->fport = port;
  s->ttx = s->tbuf;
  s->tpos = s->tbuf + len;
  return sk_maybe_write(s);
}

static int
sk_read(sock *s)
{
  switch (s->type)
    {
    case SK_TCP_ACTIVE:
      {
	struct sockaddr_in sa;
	fill_in_sockaddr(&sa, s->daddr, s->dport);
	if (connect(s->fd, (struct sockaddr *) &sa, sizeof(sa)) >= 0)
	  sk_tcp_connected(s);
	else if (errno != EINTR && errno != EAGAIN)
	  {
	    log(L_ERR "connect: %m");
	    s->err_hook(s, errno);
	  }
	return 0;
      }
    case SK_TCP_PASSIVE:
      {
	struct sockaddr_in sa;
	int al = sizeof(sa);
	int fd = accept(s->fd, (struct sockaddr *) &sa, &al);
	if (fd >= 0)
	  {
	    sock *t = sk_new(s->pool);
	    char *err;
	    t->type = SK_TCP;
	    t->fd = fd;
	    add_tail(&sock_list, &t->n);
	    s->rx_hook(t, 0);
	    if (err = sk_setup(t))
	      {
		log(L_ERR "Incoming connection: %s: %m", err);
		s->err_hook(s, errno);
		return 0;
	      }
	    sk_alloc_bufs(t);
	    return 1;
	  }
	else if (errno != EINTR && errno != EAGAIN)
	  {
	    log(L_ERR "accept: %m");
	    s->err_hook(s, errno);
	  }
	return 0;
      }
    case SK_TCP:
      {
	int c = read(s->fd, s->rpos, s->rbuf + s->rbsize - s->rpos);

	if (c < 0)
	  {
	    if (errno != EINTR && errno != EAGAIN)
	      {
		log(L_ERR "read: %m");
		s->err_hook(s, errno);
	      }
	  }
	else if (!c)
	  s->err_hook(s, 0);
	else
	  {
	    s->rpos += c;
	    if (s->rx_hook(s, s->rpos - s->rbuf))
	      s->rpos = s->rbuf;
	    return 1;
	  }
	return 0;
      }
    default:
      {
	struct sockaddr_in sa;
	int al = sizeof(sa);
	int e = recvfrom(s->fd, s->rbuf, s->rbsize, 0, (struct sockaddr *) &sa, &al);

	if (e < 0)
	  {
	    if (errno != EINTR && errno != EAGAIN)
	      {
		log(L_ERR "recvfrom: %m");
		s->err_hook(s, errno);
	      }
	    return 0;
	  }
	s->rpos = s->rbuf + e;
	get_sockaddr(&sa, &s->faddr, &s->fport);
	s->rx_hook(s, e);
	return 1;
      }
    }
}

static void
sk_write(sock *s)
{
  while (s->ttx != s->tbuf && sk_maybe_write(s) > 0)
    s->tx_hook(s);
}

void
sk_dump_all(void)
{
  node *n;
  sock *s;

  debug("Open sockets:\n");
  WALK_LIST(n, sock_list)
    {
      s = SKIP_BACK(sock, n, n);
      debug("%p ", s);
      sk_dump(&s->r);
    }
  debug("\n");
}

#undef ERR

/*
 *	Main I/O Loop
 */

void
io_init(void)
{
  init_list(&near_timers);
  init_list(&far_timers);
  init_list(&sock_list);
  now = time(NULL);
}

void
io_loop(void)
{
  fd_set rd, wr;
  struct timeval timo;
  time_t tout;
  int hi;
  sock *s;
  node *n;

  /* FIXME: Use poll() if available */

  FD_ZERO(&rd);
  FD_ZERO(&wr);
  for(;;)
    {
      now = time(NULL);
      tout = tm_first_shot();
      if (tout <= now)
	{
	  tm_shot();
	  continue;
	}
      else
	{
	  timo.tv_sec = tout - now;
	  timo.tv_usec = 0;
	}

      hi = 0;
      WALK_LIST(n, sock_list)
	{
	  s = SKIP_BACK(sock, n, n);
	  if (s->rx_hook)
	    {
	      FD_SET(s->fd, &rd);
	      if (s->fd > hi)
		hi = s->fd;
	    }
	  if (s->tx_hook && s->ttx != s->tpos)
	    {
	      FD_SET(s->fd, &wr);
	      if (s->fd > hi)
		hi = s->fd;
	    }
	}

      hi = select(hi+1, &rd, &wr, NULL, &timo);
      if (hi < 0)
	{
	  if (errno == EINTR || errno == EAGAIN)
	    continue;
	  die("select: %m");
	}
      if (hi)
	{
	  WALK_LIST(n, sock_list)
	    {
	      s = SKIP_BACK(sock, n, n);
	      if (FD_ISSET(s->fd, &rd))
		{
		  FD_CLR(s->fd, &rd);
		  while (sk_read(s))
		    ;
		}
	      if (FD_ISSET(s->fd, &wr))
		{
		  FD_CLR(s->fd, &wr);
		  sk_write(s);
		}
	    }
	}
    }
}
