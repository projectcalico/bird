
typedef s64 xxx_time;

typedef struct timer
{
  resource r;
  void (*hook)(struct timer2 *);
  void *data;

  xxx_time expires;			/* 0=inactive */
  unsigned randomize;			/* Amount of randomization */
  unsigned recurrent;			/* Timer recurrence */

  int index;
} timer;



void ev2_schedule(event *e);



timer2 *tm2_new(pool *p);
void tm2_start(timer2 *t, xxx_time after);
void tm2_stop(timer2 *t);

static inline xxx_time
tm2_remains(timer2 *t)
{
  return (t->expires > xxxnow) ? t->expires - xxxnow : 0;
}

static inline void
tm2_start_max(timer2 *t, xxx_time after)
{
  xxx_time rem = tm2_remains(t);
  tm2_start(t, MAX(rem, after));
}

static inline timer2 *
tm2_new_set(pool *p, void (*hook)(struct timer2 *), void *data, uint rec, uint rand)
{
  timer2 *t = tm2_new(p);
  t->hook = hook;
  t->data = data;
  t->recurrent = rec;
  t->randomize = rand;
  return t;
}



void sk_start(sock *s);
void sk_stop(sock *s);



struct birdloop *birdloop_new(pool *p);
void birdloop_enter(struct birdloop *loop);
void birdloop_leave(struct birdloop *loop);
void birdloop_main(struct birdloop *loop);

