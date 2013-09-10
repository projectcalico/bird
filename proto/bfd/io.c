
#include "lib/buffer.h"
#include "lib/heap.h"

struct birdloop
{
  pool *pool;

  int wakeup_fds[2];
  u8 poll_active;

  xxx_time last_time;
  xxx_time real_time;
  u8 use_monotonic_clock;

  BUFFER(timer2 *) timers;
  list event_list;
  list sock_list;
  uint sock_num;

  BUFFER(sock *) poll_sk;
  BUFFER(struct pollfd) poll_fd;
  u8 poll_changed;
  u8 close_scheduled;

};


static void times_update_alt(struct birdloop *loop);

static int
times_init(struct birdloop *loop)
{
  struct timespec ts;
  int rv;

  rv = clock_gettime(CLOCK_MONOTONIC, &ts);
  if (rv < 0)
  {
    // log(L_WARN "Monotonic clock is missing");

    loop->use_monotonic_clock = 0;
    loop->last_time = 0;
    loop->real_time = 0;
    times_update_alt(loop);
    return;
  }

  /*
  if ((tv.tv_sec < 0) || (((s64) tv.tv_sec) > ((s64) 1 << 40)))
    log(L_WARN "Monotonic clock is crazy");
  */

  loop->use_monotonic_clock = 1;
  loop->last_time = (tv.tv_sec S) + (tv.tv_nsec / 1000);
  loop->real_time = 0;
}

static void
times_update_pri(struct birdloop *loop)
{
  struct timespec ts;
  int rv;

  rv = clock_gettime(CLOCK_MONOTONIC, &ts);
  if (rv < 0)
    die("clock_gettime: %m");

  xxx_time new_time = (tv.tv_sec S) + (tv.tv_nsec / 1000);

  /*
  if (new_time < loop->last_time)
    log(L_ERR "Monotonic clock is broken");
  */

  loop->last_time = new_time;
  loop->real_time = 0;
}

static void
times_update_alt(struct birdloop *loop)
{
  struct timeval tv;
  int rv;

  rv = gettimeofday(&tv, NULL);
  if (rv < 0)
    die("gettimeofday: %m");

  xxx_time new_time = (tv.tv_sec S) + tv.tv_usec;
  xxx_time delta = new_time - loop->real_time;

  if ((delta < 0) || (delta > (60 S)))
  {
    /*
    if (loop->real_time)
      log(L_WARN "Time jump, delta %d us", (int) delta);
    */

    delta = 100 MS;
  }

  loop->last_time += delta;
  loop->real_time = new_time;
}

static void
times_update(struct birdloop *loop)
{
  if (loop->use_monotonic_clock)
    times_update_pri(loop);
  else
    times_update_alt(loop);
}



static void
pipe_new(int *pfds)
{
  int pfds[2], rv;
  sock *sk;
  
  rv = pipe(pfds);
  if (rv < 0)
    die("pipe: %m");

  if (fcntl(pfds[0], F_SETFL, O_NONBLOCK) < 0)
    die("fcntl(O_NONBLOCK): %m");

  if (fcntl(pfds[1], F_SETFL, O_NONBLOCK) < 0)
    die("fcntl(O_NONBLOCK): %m");
}

static void
wakeup_init(struct birdloop *loop)
{
  pipe_new(loop->wakeup_fds);
}

static void
wakeup_drain(struct birdloop *loop)
{
  char buf[64];
  int rv;
  
 try:
  rv = read(loop->wakeup_fds[0], buf, 64);
  if (rv < 0)
  {
    if (errno == EINTR)
      goto try;
    if (errno == EAGAIN)
      return;
    die("wakeup read: %m");
  }
  if (rv == 64)
    goto try;
}

static void
wakeup_kick(struct birdloop *loop)
{
  u64 v = 1;
  int rv;

 try:
  rv = write(loop->wakeup_fds[1], &v, sizeof(u64));
  if (rv < 0)
  {
    if (errno == EINTR)
      goto try;
    if (errno == EAGAIN)
      return;
    die("wakeup write: %m");
  }
}




static inline uint events_waiting(struct birdloop *loop)
{ return !EMPTY_LIST(loop->event_list); }

static void
events_init(struct birdloop *loop)
{
  list_init(&poll->event_list);
}

static void
events_fire(struct birdloop *loop)
{
  times_update(loop);
  ev_run_list(&loop->event_list);
}

void
ev2_schedule(event *e)
{
  if (loop->poll_active && EMPTY_LIST(loop->event_list))
    wakeup_kick(loop);

  if (e->n.next)
    rem_node(&e->n);

  add_tail(&loop->event_list, &e->n);
}


#define TIMER_LESS(a,b)		((a)->expires < (b)->expires)
#define TIMER_SWAP(heap,a,b,t)	(t = heap[a], heap[a] = heap[b], heap[b] = t, \
				   heap[a]->index = (a), heap[b]->index = (b))


static inline uint timers_count(struct birdloop *loop)
{ return loop->timers.used - 1; }

static inline timer2 *timers_first(struct birdloop *loop)
{ return (loop->timers.used > 1) ? loop->timers.data[1] : NULL; }


static void
tm2_free(resource *r)
{
  timer2 *t = (timer2 *) r;

  tm2_stop(t);
}

static void
tm2_dump(resource *r)
{
  timer2 *t = (timer2 *) r;

  debug("(code %p, data %p, ", t->hook, t->data);
  if (t->randomize)
    debug("rand %d, ", t->randomize);
  if (t->recurrent)
    debug("recur %d, ", t->recurrent);
  if (t->expires)
    debug("expires in %d sec)\n", t->expires - xxx_now);
  else
    debug("inactive)\n");
}

static struct resclass tm2_class = {
  "Timer",
  sizeof(timer),
  tm2_free,
  tm2_dump,
  NULL,
  NULL
};

timer2 *
tm2_new(pool *p)
{
  timer2 *t = ralloc(p, &tm2_class);
  t->index = -1;
  return t;
}

void
tm2_start(timer2 *t, xxx_time after)
{
  xxx_time when = loop->last_time + after;
  uint tc = timers_count(loop);

  if (!t->expires)
  {
    t->index = ++tc;
    t->expires = when;
    BUFFER_PUSH(loop->timers) = t;
    HEAP_INSERT(loop->timers.data, tc, timer2 *, TIMER_LESS, TIMER_SWAP);
  }
  else if (t->expires < when)
  {
    t->expires = when;
    HEAP_INCREASE(loop->timers.data, tc, timer2 *, TIMER_LESS, TIMER_SWAP, t->index);
  }
  else if (t->expires > when)
  {
    t->expires = when;
    HEAP_DECREASE(loop->timers.data, tc, timer2 *, TIMER_LESS, TIMER_SWAP, t->index);
  }

  if (loop->poll_active && (t->index == 1))
    wakeup_kick(loop);
}

void
tm2_stop(timer2 *t)
{
  if (!t->expires)
    return;

  uint tc = timers_count(XXX);
  HEAP_DELETE(loop->timers.data, tc, timer2 *, TIMER_LESS, TIMER_SWAP, t->index);
  BUFFER_POP(loop->timers);

  t->index = -1;
  t->expires = 0;
}

static void
timers_init(struct birdloop *loop)
{
  BUFFER_INIT(loop->timers, loop->pool, 4);
  BUFFER_PUSH(loop->timers) = NULL;
}

static void
timers_fire(struct birdloop *loop)
{
  xxx_time base_time;
  timer2 *t;

  times_update(loop);
  base_time = loop->last_time;

  while (t = timers_first(loop))
  {
    if (t->expires > base_time)
      return;

    if (t->recurrent)
    {
      xxx_time after = t->recurrent;
      xxx_time delta = loop->last_time - t->expires;
      
      if (t->randomize)
	after += random() % (t->randomize + 1);

      if (delta > after)
	delta = 0;

      tm2_start(t, after - delta);
    }
    else
      tm2_stop(t);

    t->hook(t);
  }
}


static void
sockets_init(struct birdloop *loop)
{
  list_init(&poll->sock_list);
  poll->sock_num = 0;

  BUFFER_INIT(loop->poll_sk, loop->pool, 4);
  BUFFER_INIT(loop->poll_fd, loop->pool, 4);
  poll_changed = 0;
}

static void
sockets_add(struct birdloop *loop, sock *s)
{
  add_tail(&loop->sock_list, &s->n);
  loop->sock_num++;

  s->index = -1;
  loop->poll_changed = 1;

  if (loop->poll_active)
    wakeup_kick(loop);
}

void
sk_start(sock *s)
{
  sockets_add(xxx_loop, s);
}

static void
sockets_remove(struct birdloop *loop, sock *s)
{
  rem_node(&s->n);
  loop->sock_num--;

  if (s->index >= 0)
    s->poll_sk.data[sk->index] = NULL;

  s->index = -1;
  loop->poll_changed = 1;

  /* Wakeup moved to sk_stop() */
}

void
sk_stop(sock *s)
{
  sockets_remove(xxx_loop, s);

  if (loop->poll_active)
  {
    loop->close_scheduled = 1;
    wakeup_kick(loop);
  }
  else
    close(s->fd);

  s->fd = -1;
}

static inline uint sk_want_events(sock *s)
{ return (s->rx_hook ? POLLIN : 0) | ((s->ttx != s->tpos) ? POLLOUT : 0); }

static void
sockets_update(struct birdloop *loop, sock *s)
{
  if (s->index >= 0)
    s->poll_fd.data[s->index].events = sk_want_events(s);
}

static void
sockets_prepare(struct birdloop *loop)
{
  BUFFER_SET(loop->poll_sk, loop->sock_num + 1);
  BUFFER_SET(loop->poll_fd, loop->sock_num + 1);

  struct pollfd *pfd = loop->poll_fd.data;
  sock **psk = loop->poll_sk.data;
  int i = 0;
  node *n;

  WALK_LIST(n, &loop->sock_list)
  {
    sock *s = SKIP_BACK(sock, n, n);

    ASSERT(i < loop->sock_num);

    s->index = i;
    *psk = s;
    pfd->fd = s->fd;
    pfd->events = sk_want_events(s);
    pfd->revents = 0;

    pfd++;
    psk++;
    i++;
  }

  ASSERT(i == loop->sock_num);

  /* Add internal wakeup fd */
  *psk = NULL;
  pfd->fd = loop->wakeup_fds[0];
  pfd->events = POLLIN;
  pfd->revents = 0;

  loop->poll_changed = 0;
}

static void
sockets_close_fds(struct birdloop *loop)
{
  struct pollfd *pfd = loop->poll_fd.data;
  sock **psk = loop->poll_sk.data;
  int poll_num = loop->poll_fd.used - 1;

  int i;
  for (i = 0; i < poll_num; i++)
    if (psk[i] == NULL)
      close(pfd[i].fd);

  loop->close_scheduled = 0;
}


static void
sockets_fire(struct birdloop *loop)
{
  struct pollfd *pfd = loop->poll_fd.data;
  sock **psk = loop->poll_sk.data;
  int poll_num = loop->poll_fd.used - 1;

  times_update(loop);

  /* Last fd is internal wakeup fd */
  if (pfd[loop->sock_num].revents & POLLIN)
    wakeup_drain(loop);

  int i;
  for (i = 0; i < poll_num; pfd++, psk++, i++)
  {
    int e = 1;

    if (! pfd->revents)
      continue;

    if (pfd->revents & POLLNVAL)
      die("poll: invalid fd %d", pfd->fd);

    if (pfd->revents & POLLIN)
      while (e && *psk && (*psk)->rx_hook)
	e = sk_read(*psk);

    e = 1;
    if (pfd->revents & POLLOUT)
      while (e && *psk)
	e = sk_write(*psk);
  }
}


struct birdloop *
birdloop_new(pool *p)
{
  struct birdloop *loop = mb_allocz(p, sizeof(struct birdloop));
  p->pool = p;

  times_init(loop);
  wakeup_init(loop);

  events_init(loop);
  timers_init(loop);
  sockets_init(loop);

  return loop;
}

void
birdloop_enter(struct birdloop *loop)
{
  pthread_mutex_lock(loop->mutex);
}

void
birdloop_leave(struct birdloop *loop)
{
  pthread_mutex_unlock(loop->mutex);
}


void
birdloop_main(struct birdloop *loop)
{
  timer2 *t;
  int timeout;

  while (1)
  {
    events_fire(loop);
    timers_fire(loop);

    times_update(loop);
    if (events_waiting(loop))
      timeout = 0;
    else if (t = timers_first(loop))
      timeout = (tm2_remains(t) TO_MS) + 1;
    else
      timeout = -1;

    if (loop->poll_changed)
      sockets_prepare(loop);

    loop->poll_active = 1;
    pthread_mutex_unlock(loop->mutex);

  try:
    rv = poll(loop->poll_fd.data, loop->poll_fd.used, timeout);
    if (rv < 0)
    {
      if (errno == EINTR || errno == EAGAIN)
	goto try;
      die("poll: %m");
    }

    pthread_mutex_lock(loop->mutex);
    loop->poll_active = 0;

    if (loop->close_scheduled)
      sockets_close_fds(loop);

    if (rv)
      sockets_fire(loop);

    timers_fire(loop);
  }
}



