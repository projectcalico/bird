/*
 *	BIRD Internet Routing Daemon -- Unix I/O
 *
 *	(c) 1998--2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>

#include "nest/bird.h"
#include "lib/lists.h"
#include "lib/resource.h"
#include "lib/timer.h"
#include "lib/socket.h"
#include "lib/event.h"
#include "lib/string.h"
#include "nest/iface.h"

#include "lib/unix.h"
#include "lib/sysio.h"

/*
 *	Random Numbers
 */

u32
random_u32(void)
{
  long int rand_low, rand_high;

  rand_low = random();
  rand_high = random();
  return (rand_low & 0xffff) | ((rand_high & 0xffff) << 16);
}

/*
 *	Tracked Files
 */

struct rfile {
  resource r;
  FILE *f;
};

static void
rf_free(resource *r)
{
  struct rfile *a = (struct rfile *) r;

  fclose(a->f);
}

static void
rf_dump(resource *r)
{
  struct rfile *a = (struct rfile *) r;

  debug("(FILE *%p)\n", a->f);
}

static struct resclass rf_class = {
  "FILE",
  sizeof(struct rfile),
  rf_free,
  rf_dump
};

void *
tracked_fopen(pool *p, char *name, char *mode)
{
  FILE *f = fopen(name, mode);

  if (f)
    {
      struct rfile *r = ralloc(p, &rf_class);
      r->f = f;
    }
  return f;
}

/*
 *	Timers
 */

#define NEAR_TIMER_LIMIT 4

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

  debug("(code %p, data %p, ", t->hook, t->data);
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
      bird_clock_t limit = now + NEAR_TIMER_LIMIT;
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

bird_clock_t
tm_parse_date(char *x)
{
  struct tm tm;
  int n;
  time_t t;

  if (sscanf(x, "%d-%d-%d%n", &tm.tm_mday, &tm.tm_mon, &tm.tm_year, &n) != 3 || x[n])
    return 0;
  tm.tm_mon--;
  tm.tm_year -= 1900;
  tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
  t = mktime(&tm);
  if (t == (time_t) -1)
    return 0;
  return t;
}

void
tm_format_date(char *x, bird_clock_t t)
{
  struct tm *tm;

  tm = localtime(&t);
  bsprintf(x, "%02d-%02d-%04d", tm->tm_mday, tm->tm_mon+1, tm->tm_year+1900);
}

void
tm_format_datetime(char *x, bird_clock_t t)
{
  struct tm *tm;

  tm = localtime(&t);
  if (strftime(x, TM_DATETIME_BUFFER_SIZE, "%d-%m-%Y %H:%M:%S", tm) == TM_DATETIME_BUFFER_SIZE)
    strcpy(x, "<too-long>");
}

void
tm_format_reltime(char *x, bird_clock_t t)
{
  struct tm *tm;
  bird_clock_t delta = now - t;
  static char *month_names[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

  tm = localtime(&t);
  if (delta < 0)
    strcpy(x, "?fut?");
  else if (delta < 20*3600)
    bsprintf(x, "%02d:%02d", tm->tm_hour, tm->tm_min);
  else if (delta < 360*86400)
    bsprintf(x, "%s%02d", month_names[tm->tm_mon], tm->tm_mday);
  else
    bsprintf(x, "%d", tm->tm_year+1900);
}

/*
 *	Sockets
 */

#ifndef SOL_IP
#define SOL_IP IPPROTO_IP
#endif

static list sock_list;

static void
sk_free(resource *r)
{
  sock *s = (sock *) r;

  if (s->fd >= 0)
    {
      close(s->fd);
      rem_node(&s->n);
    }
}

static void
sk_dump(resource *r)
{
  sock *s = (sock *) r;
  static char *sk_type_names[] = { "TCP<", "TCP>", "TCP", "UDP", "UDP/MC", "IP", "IP/MC", "MAGIC", "UNIX<", "UNIX", "DEL!" };

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
  s->entered = 0;
  return s;
}

#define ERR(x) do { err = x; goto bad; } while(0)
#define WARN(x) log(L_WARN "sk_setup: %s: %m", x)

#ifdef IPV6

void
fill_in_sockaddr(sockaddr *sa, ip_addr a, unsigned port)
{
  sa->sin6_family = AF_INET6;
  sa->sin6_port = htons(port);
  sa->sin6_flowinfo = 0;
  set_inaddr(&sa->sin6_addr, a);
}

void
get_sockaddr(sockaddr *sa, ip_addr *a, unsigned *port)
{
  if (sa->sin6_family != AF_INET6)
    bug("get_sockaddr called for wrong address family");
  if (port)
    *port = ntohs(sa->sin6_port);
  memcpy(a, &sa->sin6_addr, sizeof(*a));
  ipa_ntoh(*a);
}

#else

void
fill_in_sockaddr(sockaddr *sa, ip_addr a, unsigned port)
{
  sa->sin_family = AF_INET;
  sa->sin_port = htons(port);
  set_inaddr(&sa->sin_addr, a);
}

void
get_sockaddr(sockaddr *sa, ip_addr *a, unsigned *port)
{
  if (sa->sin_family != AF_INET)
    bug("get_sockaddr called for wrong address family");
  if (port)
    *port = ntohs(sa->sin_port);
  memcpy(a, &sa->sin_addr.s_addr, sizeof(*a));
  ipa_ntoh(*a);
}

#endif

static char *
sk_setup(sock *s)
{
  int fd = s->fd;
  int one = 1;
  char *err;

  if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
    ERR("fcntl(O_NONBLOCK)");
  if (s->type == SK_UNIX)
    return NULL;
#ifdef IPV6
  if (s->ttl >= 0 && s->type != SK_UDP_MC && s->type != SK_IP_MC &&
      setsockopt(fd, SOL_IPV6, IPV6_UNICAST_HOPS, &s->ttl, sizeof(s->ttl)) < 0)
    ERR("IPV6_UNICAST_HOPS");
#else
  if ((s->tos >= 0) && setsockopt(fd, SOL_IP, IP_TOS, &s->tos, sizeof(s->tos)) < 0)
    WARN("IP_TOS");
  if (s->ttl >= 0 && setsockopt(fd, SOL_IP, IP_TTL, &s->ttl, sizeof(s->ttl)) < 0)
    ERR("IP_TTL");
  if (s->ttl == 1 && setsockopt(fd, SOL_SOCKET, SO_DONTROUTE, &one, sizeof(one)) < 0)
    ERR("SO_DONTROUTE");
#endif
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

static void
sk_tcp_connected(sock *s)
{
  s->type = SK_TCP;
  sk_alloc_bufs(s);
  s->tx_hook(s);
}

static int
sk_passive_connected(sock *s, struct sockaddr *sa, int al, int type)
{
  int fd = accept(s->fd, sa, &al);
  if (fd >= 0)
    {
      sock *t = sk_new(s->pool);
      char *err;
      t->type = type;
      t->fd = fd;
      t->ttl = s->ttl;
      t->tos = s->tos;
      t->rbsize = s->rbsize;
      t->tbsize = s->tbsize;
      if (type == SK_TCP)
	get_sockaddr((sockaddr *) sa, &t->daddr, &t->dport);
      add_tail(&sock_list, &t->n);
      if (err = sk_setup(t))
	{
	  log(L_ERR "Incoming connection: %s: %m", err);
	  rfree(t);
	  return 1;
	}
      sk_alloc_bufs(t);
      s->rx_hook(t, 0);
      return 1;
    }
  else if (errno != EINTR && errno != EAGAIN)
    {
      log(L_ERR "accept: %m");
      s->err_hook(s, errno);
    }
  return 0;
}

int
sk_open(sock *s)
{
  int fd;
  sockaddr sa;
  int one = 1;
  int type = s->type;
  int has_src = ipa_nonzero(s->saddr) || s->sport;
  char *err;

  switch (type)
    {
    case SK_TCP_ACTIVE:
      s->ttx = "";			/* Force s->ttx != s->tpos */
      /* Fall thru */
    case SK_TCP_PASSIVE:
      fd = socket(BIRD_PF, SOCK_STREAM, IPPROTO_TCP);
      break;
    case SK_UDP:
    case SK_UDP_MC:
      fd = socket(BIRD_PF, SOCK_DGRAM, IPPROTO_UDP);
      break;
    case SK_IP:
    case SK_IP_MC:
      fd = socket(BIRD_PF, SOCK_RAW, s->dport);
      break;
    case SK_MAGIC:
      fd = s->fd;
      break;
    default:
      bug("sk_open() called for invalid sock type %d", type);
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
#ifdef IPV6
	bug("IPv6 has no broadcasts");
#else
	if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one)) < 0)
	  ERR("SO_BROADCAST");
#endif
      break;
    case SK_UDP_MC:
    case SK_IP_MC:
      {
#ifdef IPV6
	/* Fortunately, IPv6 socket interface is recent enough and therefore standardized */
	ASSERT(s->iface && s->iface->addr);
	if (ipa_nonzero(s->daddr))
	  {
	    int t = s->iface->index;
	    int zero = 0;
	    if (setsockopt(fd, SOL_IPV6, IPV6_MULTICAST_HOPS, &s->ttl, sizeof(s->ttl)) < 0)
	      ERR("IPV6_MULTICAST_HOPS");
	    if (setsockopt(fd, SOL_IPV6, IPV6_MULTICAST_LOOP, &zero, sizeof(zero)) < 0)
	      ERR("IPV6_MULTICAST_LOOP");
	    if (setsockopt(fd, SOL_IPV6, IPV6_MULTICAST_IF, &t, sizeof(t)) < 0)
	      ERR("IPV6_MULTICAST_IF");
	  }
	if (has_src)
	  {
	    struct ipv6_mreq mreq;
	    set_inaddr(&mreq.ipv6mr_multiaddr, s->daddr);
#ifdef CONFIG_IPV6_GLIBC_20
	    mreq.ipv6mr_ifindex = s->iface->index;
#else
	    mreq.ipv6mr_interface = s->iface->index;
#endif
	    if (setsockopt(fd, SOL_IPV6, IPV6_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
	      ERR("IPV6_ADD_MEMBERSHIP");
	  }
#else
	/* With IPv4 there are zillions of different socket interface variants. Ugh. */
	ASSERT(s->iface && s->iface->addr);
	if (err = sysio_mcast_join(s))
	  goto bad;
#endif
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
      else if (errno != EINTR && errno != EAGAIN && errno != EINPROGRESS)
	ERR("connect");
      break;
    case SK_TCP_PASSIVE:
      if (listen(fd, 8))
	ERR("listen");
      break;
    case SK_MAGIC:
      break;
    default:
      sk_alloc_bufs(s);
#ifdef IPV6
#ifdef IPV6_MTU_DISCOVER
      {
	int dont = IPV6_PMTUDISC_DONT;
	if (setsockopt(fd, SOL_IPV6, IPV6_MTU_DISCOVER, &dont, sizeof(dont)) < 0)
	  ERR("IPV6_MTU_DISCOVER");
      }
#endif
#else
#ifdef IP_PMTUDISC
      {
	int dont = IP_PMTUDISC_DONT;
	if (setsockopt(fd, SOL_IP, IP_PMTUDISC, &dont, sizeof(dont)) < 0)
	  ERR("IP_PMTUDISC");
      }
#endif
#endif
    }

  add_tail(&sock_list, &s->n);
  return 0;

bad:
  log(L_ERR "sk_open: %s: %m", err);
  close(fd);
  s->fd = -1;
  return -1;
}

int
sk_open_unix(sock *s, char *name)
{
  int fd;
  struct sockaddr_un sa;
  char *err;

  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    die("sk_open_unix: socket: %m");
  s->fd = fd;
  if (err = sk_setup(s))
    goto bad;
  unlink(name);
  sa.sun_family = AF_UNIX;
  strcpy(sa.sun_path, name);
  if (bind(fd, (struct sockaddr *) &sa, sizeof(sa)) < 0)
    ERR("bind");
  if (listen(fd, 8))
    ERR("listen");
  add_tail(&sock_list, &s->n);
  return 0;

bad:
  log(L_ERR "sk_open_unix: %s: %m", err);
  close(fd);
  s->fd = -1;
  return -1;
}

void
sk_close(sock *s)
{
  if (s && s->entered)
    s->type = SK_DELETED;
  else
    rfree(s);
}

static int
sk_maybe_write(sock *s)
{
  int e;

  switch (s->type)
    {
    case SK_TCP:
    case SK_MAGIC:
    case SK_UNIX:
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
	sockaddr sa;

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
      bug("sk_maybe_write: unknown socket type %d", s->type);
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
    case SK_TCP_PASSIVE:
      {
	sockaddr sa;
	return sk_passive_connected(s, (struct sockaddr *) &sa, sizeof(sa), SK_TCP);
      }
    case SK_UNIX_PASSIVE:
      {
	struct sockaddr_un sa;
	return sk_passive_connected(s, (struct sockaddr *) &sa, sizeof(sa), SK_UNIX);
      }
    case SK_TCP:
    case SK_UNIX:
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
    case SK_MAGIC:
      return s->rx_hook(s, 0);
    case SK_DELETED:
      return 0;
    default:
      {
	sockaddr sa;
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
  switch (s->type)
    {
    case SK_TCP_ACTIVE:
      {
	sockaddr sa;
	fill_in_sockaddr(&sa, s->daddr, s->dport);
	if (connect(s->fd, (struct sockaddr *) &sa, sizeof(sa)) >= 0)
	  sk_tcp_connected(s);
	else if (errno != EINTR && errno != EAGAIN && errno != EINPROGRESS)
	  {
	    log(L_ERR "connect: %m");
	    s->err_hook(s, errno);
	  }
	break;
      }
    case SK_DELETED:
      return;
    default:
      while (s->ttx != s->tpos && sk_maybe_write(s) > 0)
	s->tx_hook(s);
    }
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
#undef WARN

/*
 *	Main I/O Loop
 */

volatile int async_config_flag;		/* Asynchronous reconfiguration/dump scheduled */
volatile int async_dump_flag;

void
io_init(void)
{
  init_list(&near_timers);
  init_list(&far_timers);
  init_list(&sock_list);
  init_list(&global_event_list);
  krt_io_init();
  now = time(NULL);
  srandom((int) now);
}

void
io_loop(void)
{
  fd_set rd, wr;
  struct timeval timo;
  time_t tout;
  int hi, events;
  sock *s;
  node *n, *p;

  FD_ZERO(&rd);
  FD_ZERO(&wr);
  for(;;)
    {
      events = ev_run_list(&global_event_list);
      now = time(NULL);
      tout = tm_first_shot();
      if (tout <= now)
	{
	  tm_shot();
	  continue;
	}
      timo.tv_sec = events ? 0 : tout - now;
      timo.tv_usec = 0;

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

      /*
       * Yes, this is racy. But even if the signal comes before this test
       * and entering select(), it gets caught on the next timer tick.
       */

      if (async_config_flag)
	{
	  async_config();
	  async_config_flag = 0;
	  continue;
	}
      if (async_dump_flag)
	{
	  async_dump();
	  async_dump_flag = 0;
	  continue;
	}
      if (async_shutdown_flag)
	{
	  async_shutdown();
	  async_shutdown_flag = 0;
	  continue;
	}

      /* And finally enter select() to find active sockets */

      hi = select(hi+1, &rd, &wr, NULL, &timo);
      if (hi < 0)
	{
	  if (errno == EINTR || errno == EAGAIN)
	    continue;
	  die("select: %m");
	}
      if (hi)
	{
	  WALK_LIST_DELSAFE(n, p, sock_list)
	    {
	      s = SKIP_BACK(sock, n, n);
	      s->entered = 1;
	      if (FD_ISSET(s->fd, &rd))
		{
		  FD_CLR(s->fd, &rd);
		  while (sk_read(s))
		    ;
		}
	      if (s->type != SK_DELETED && FD_ISSET(s->fd, &wr))
		{
		  FD_CLR(s->fd, &wr);
		  sk_write(s);
		}
	      s->entered = 0;
	      if (s->type == SK_DELETED)
		rfree(s);
	    }
	}
    }
}
