/*
 *	BIRD -- Protocols
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include <string.h>

#include "nest/bird.h"
#include "nest/protocol.h"
#include "lib/resource.h"
#include "lib/lists.h"
#include "lib/event.h"
#include "conf/conf.h"
#include "nest/route.h"
#include "nest/iface.h"
#include "nest/cli.h"
#include "filter/filter.h"

static pool *proto_pool;

list protocol_list;
list proto_list;

static list inactive_proto_list;
static list initial_proto_list;
static list flush_proto_list;

static int proto_shutdown_counter;

static event *proto_flush_event;

static char *p_states[] = { "DOWN", "START", "UP", "STOP" };
static char *c_states[] = { "HUNGRY", "FEEDING", "HAPPY", "FLUSHING" };

static int proto_flush_all(void *);

static void
proto_enqueue(list *l, struct proto *p)
{
  int pri = p->proto->priority;

  if (!pri)
    add_tail(l, &p->n);
  else
    {
      struct proto *q = HEAD(*l);
      while (q->n.next && q->proto->priority >= pri)
	q = (struct proto *) q->n.next;
      insert_node(&p->n, q->n.prev);
    }
  p->last_state_change = now;
}

static void
proto_relink(struct proto *p)
{
  list *l;

  rem_node(&p->n);
  switch (p->core_state)
    {
    case FS_HAPPY:
      l = &proto_list;
      break;
    case FS_FLUSHING:
      l = &flush_proto_list;
      break;
    default:
      l = &inactive_proto_list;
    }
  proto_enqueue(l, p);
}

void *
proto_new(struct proto_config *c, unsigned size)
{
  struct protocol *pr = c->protocol;
  struct proto *p = mb_allocz(proto_pool, size);

  p->cf = c;
  p->debug = c->debug;
  p->name = c->name;
  p->preference = c->preference;
  p->disabled = c->disabled;
  p->proto = pr;
  p->table = c->table->table;
  p->in_filter = c->in_filter;
  p->out_filter = c->out_filter;
  c->proto = p;
  return p;
}

static void
proto_init_instance(struct proto *p)
{
  /* Here we cannot use p->cf->name since it won't survive reconfiguration */
  p->pool = rp_new(proto_pool, p->proto->name);
  p->attn = ev_new(p->pool);
  p->attn->data = p;
}

struct announce_hook *
proto_add_announce_hook(struct proto *p, struct rtable *t)
{
  struct announce_hook *h;

  if (!p->rt_notify)
    return NULL;
  DBG("Connecting protocol %s to table %s\n", p->name, t->name);
  h = mb_alloc(p->pool, sizeof(struct announce_hook));
  h->table = t;
  h->proto = p;
  h->next = p->ahooks;
  p->ahooks = h;
  add_tail(&t->hooks, &h->n);
  return h;
}

static void
proto_flush_hooks(struct proto *p)
{
  struct announce_hook *h;

  for(h=p->ahooks; h; h=h->next)
    rem_node(&h->n);
  p->ahooks = NULL;
}

void *
proto_config_new(struct protocol *pr, unsigned size)
{
  struct proto_config *c = cfg_allocz(size);

  add_tail(&new_config->protos, &c->n);
  c->global = new_config;
  c->protocol = pr;
  c->debug = pr->debug;
  c->name = pr->name;
  c->out_filter = FILTER_REJECT;
  c->table = c->global->master_rtc;
  return c;
}

void
protos_preconfig(struct config *c)
{
  struct protocol *p;

  init_list(&proto_list);
  init_list(&inactive_proto_list);
  init_list(&initial_proto_list);
  init_list(&flush_proto_list);
  debug("Protocol preconfig:");
  WALK_LIST(p, protocol_list)
    {
      debug(" %s", p->name);
      p->name_counter = 0;
      if (p->preconfig)
	p->preconfig(p, c);
    }
  debug("\n");
}

void
protos_postconfig(struct config *c)
{
  struct proto_config *x;
  struct protocol *p;

  debug("Protocol postconfig:");
  WALK_LIST(x, c->protos)
    {
      debug(" %s", x->name);
      p = x->protocol;
      if (p->postconfig)
	p->postconfig(x);
    }
  debug("\n");
}

void
protos_commit(struct config *c)
{
  struct proto_config *x;
  struct protocol *p;
  struct proto *q;

  debug("Protocol commit:");
  WALK_LIST(x, c->protos)
    {
      debug(" %s", x->name);
      p = x->protocol;
      q = p->init(x);
      q->proto_state = PS_DOWN;
      q->core_state = FS_HUNGRY;
      proto_enqueue(&initial_proto_list, q);
      /*
       *  HACK ALERT!  In case of multiple kernel routing tables,
       *  the kernel syncer acts as multiple protocols which cooperate
       *  with each other.  In order to speed up their initialization,
       *  we need to know when we're initializing the last one, hence
       *  the startup counter.
       */
      if (!q->disabled)
	p->startup_counter++;
    }
  debug("\n");
}

static void
proto_rethink_goal(struct proto *p)
{
  struct protocol *q = p->proto;

  if (p->core_state == p->core_goal)
    return;
  if (p->core_goal == FS_HAPPY)		/* Going up */
    {
      if (p->core_state == FS_HUNGRY && p->proto_state == PS_DOWN)
	{
	  DBG("Kicking %s up\n", p->name);
	  ASSERT(q->startup_counter > 0);
	  q->startup_counter--;
	  proto_init_instance(p);
	  proto_notify_state(p, (q->start ? q->start(p) : PS_UP));
	}
    }
  else 					/* Going down */
    {
      if (p->proto_state == PS_START || p->proto_state == PS_UP)
	{
	  DBG("Kicking %s down\n", p->name);
	  proto_notify_state(p, (q->shutdown ? q->shutdown(p) : PS_DOWN));
	}
    }
}

static void
proto_set_goal(struct proto *p, unsigned goal)
{
  if (p->disabled || shutting_down)
    goal = FS_HUNGRY;
  p->core_goal = goal;
  proto_rethink_goal(p);
}

void
protos_start(void)
{
  struct proto *p, *n;

  debug("Protocol start\n");
  WALK_LIST_DELSAFE(p, n, initial_proto_list)
    proto_set_goal(p, FS_HAPPY);
}

void
protos_shutdown(void)
{
  struct proto *p, *n;

  debug("Protocol shutdown\n");
  WALK_LIST_BACKWARDS_DELSAFE(p, n, inactive_proto_list)
    if (p->core_state != FS_HUNGRY || p->proto_state != PS_DOWN)
    {
      proto_shutdown_counter++;
      proto_set_goal(p, FS_HUNGRY);
    }
  WALK_LIST_BACKWARDS_DELSAFE(p, n, proto_list)
    {
      proto_shutdown_counter++;
      proto_set_goal(p, FS_HUNGRY);
    }
}

void
protos_dump_all(void)
{
  struct proto *p;

  debug("Protocols:\n");

  WALK_LIST(p, proto_list)
    {
      debug("  protocol %s (pri=%d): state %s/%s\n", p->name, p->proto->priority,
	    p_states[p->proto_state], c_states[p->core_state]);
      if (p->in_filter)
	debug("\tInput filter: %s\n", filter_name(p->in_filter));
      if (p->out_filter != FILTER_REJECT)
	debug("\tOutput filter: %s\n", filter_name(p->out_filter));
      if (p->disabled)
	debug("\tDISABLED\n");
      else if (p->proto->dump)
	p->proto->dump(p);
    }
  WALK_LIST(p, inactive_proto_list)
    debug("  inactive %s: state %s/%s\n", p->name, p_states[p->proto_state], c_states[p->core_state]);
  WALK_LIST(p, initial_proto_list)
    debug("  initial %s\n", p->name);
}

void
protos_build(void)
{
  init_list(&protocol_list);
  add_tail(&protocol_list, &proto_device.n);
#ifdef CONFIG_RIP
  add_tail(&protocol_list, &proto_rip.n);
#endif
#ifdef CONFIG_STATIC
  add_tail(&protocol_list, &proto_static.n);
#endif
#ifdef CONFIG_OSPF
  add_tail(&protocol_list, &proto_ospf.n);
#endif
  proto_pool = rp_new(&root_pool, "Protocols");
  proto_flush_event = ev_new(proto_pool);
  proto_flush_event->hook = proto_flush_all;
}

static void
proto_fell_down(struct proto *p)
{
  DBG("Protocol %s down\n", p->name);
  if (!--proto_shutdown_counter)
    protos_shutdown_notify();
  proto_rethink_goal(p);
}

static int
proto_feed(void *P)
{
  struct proto *p = P;

  DBG("Feeding protocol %s\n", p->name);
  proto_add_announce_hook(p, p->table);
  if_feed_baby(p);
  rt_feed_baby(p);
  p->core_state = FS_HAPPY;
  proto_relink(p);
  DBG("Protocol %s up and running\n", p->name);
  return 0;
}

void
proto_notify_state(struct proto *p, unsigned ps)
{
  unsigned ops = p->proto_state;
  unsigned cs = p->core_state;

  DBG("%s reporting state transition %s/%s -> */%s\n", p->name, c_states[cs], p_states[ops], p_states[ps]);
  if (ops == ps)
    return;

  switch (ps)
    {
    case PS_DOWN:
      if (cs == FS_HUNGRY)		/* Shutdown finished */
	proto_fell_down(p);
      else if (cs == FS_FLUSHING)	/* Still flushing... */
	;
      else				/* Need to start flushing */
	goto schedule_flush;
      break;
    case PS_START:
      ASSERT(ops == PS_DOWN);
      ASSERT(cs == FS_HUNGRY);
      break;
    case PS_UP:
      ASSERT(ops == PS_DOWN || ops == PS_START);
      ASSERT(cs == FS_HUNGRY);
      DBG("%s: Scheduling meal\n", p->name);
      if (p->proto->priority)		/* FIXME: Terrible hack to get synchronous device/kernel startup! */
	{
	  p->proto_state = ps;
	  p->core_state = FS_FEEDING;
	  proto_feed(p);
	  return;
	}
      cs = FS_FEEDING;
      p->attn->hook = proto_feed;
      ev_schedule(p->attn);
      break;
    case PS_STOP:
      if (cs == FS_FEEDING || cs == FS_HAPPY)
	{
	schedule_flush:
	  DBG("%s: Scheduling flush\n", p->name);
	  proto_flush_hooks(p);
	  cs = FS_FLUSHING;
	  ev_schedule(proto_flush_event);
	}
      break;
    default:
    error:
      bug("Invalid state transition for %s from %s/%s to */%s", p->name, c_states[cs], p_states[ops], p_states[ps]);
    }
  p->proto_state = ps;
  p->core_state = cs;
  proto_relink(p);
}

static int
proto_flush_all(void *unused)
{
  struct proto *p;

  rt_prune_all();
  neigh_prune();
  while ((p = HEAD(flush_proto_list))->n.next)
    {
      DBG("Flushing protocol %s\n", p->name);
      rfree(p->pool);
      p->pool = NULL;
      p->core_state = FS_HUNGRY;
      proto_relink(p);
      proto_fell_down(p);
    }
  return 0;
}

/*
 *  CLI Commands
 */

static char *
proto_state_name(struct proto *p)
{
#define P(x,y) ((x << 4) | y)
  switch (P(p->proto_state, p->core_state))
    {
    case P(PS_DOWN, FS_HUNGRY):		return "down";
    case P(PS_START, FS_HUNGRY):	return "start";
    case P(PS_UP, FS_HUNGRY):
    case P(PS_UP, FS_FEEDING):		return "feed";
    case P(PS_STOP, FS_HUNGRY):		return "stop";
    case P(PS_UP, FS_HAPPY):		return "up";
    case P(PS_STOP, FS_FLUSHING):
    case P(PS_DOWN, FS_FLUSHING):	return "flush";
    default:      			return "???";
    }
#undef P
}

static void
proto_do_show(struct proto *p, int verbose)
{
  byte buf[256], reltime[TM_RELTIME_BUFFER_SIZE];

  buf[0] = 0;
  if (p->proto->get_status)
    p->proto->get_status(p, buf);
  tm_format_reltime(reltime, p->last_state_change);
  cli_msg(-1002, "%-8s %-8s %-8s %-5s %-5s %s",
	  p->name,
	  p->proto->name,
	  p->table->name,
	  proto_state_name(p),
	  reltime,
	  buf);
  if (verbose)
    {
      cli_msg(-1006, "\tPreference: %d", p->preference);
      cli_msg(-1006, "\tInput filter: %s", filter_name(p->in_filter));
      cli_msg(-1006, "\tOutput filter: %s", filter_name(p->out_filter));
    }
}

static void
proto_do_show_list(list *l, int verbose)
{
  struct proto *p;

  WALK_LIST(p, *l)
    proto_do_show(p, verbose);
}

void
proto_show(struct symbol *s, int verbose)
{
  if (s && s->class != SYM_PROTO)
    {
      cli_msg(9002, "%s is not a protocol", s->name);
      return;
    }
  cli_msg(-2002, "name     proto    table    state since info");
  if (s)
    proto_do_show(((struct proto_config *)s->def)->proto, verbose);
  else
    {
      proto_do_show_list(&proto_list, verbose);
      proto_do_show_list(&flush_proto_list, verbose);
      proto_do_show_list(&inactive_proto_list, verbose);
    }
  cli_msg(0, "");
}
