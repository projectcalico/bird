/*
 *	BIRD -- Protocols
 *
 *	(c) 1998--2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#undef LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/protocol.h"
#include "lib/resource.h"
#include "lib/lists.h"
#include "lib/event.h"
#include "lib/string.h"
#include "conf/conf.h"
#include "nest/route.h"
#include "nest/iface.h"
#include "nest/cli.h"
#include "filter/filter.h"

static pool *proto_pool;

static list protocol_list;
static list proto_list;

#define WALK_PROTO_LIST(p) do {							\
	node *nn;								\
	WALK_LIST(nn, proto_list) {						\
		struct proto *p = SKIP_BACK(struct proto, glob_node, nn);
#define WALK_PROTO_LIST_END } } while(0)

#define PD(pr, msg, args...) do { if (pr->debug & D_STATES) { log(L_TRACE "%s: " msg, pr->name , ## args); } } while(0)

list active_proto_list;
static list inactive_proto_list;
static list initial_proto_list;
static list flush_proto_list;

static event *proto_flush_event;

static char *p_states[] = { "DOWN", "START", "UP", "STOP" };
static char *c_states[] = { "HUNGRY", "FEEDING", "HAPPY", "FLUSHING" };

static void proto_flush_all(void *);
static void proto_rethink_goal(struct proto *p);
static char *proto_state_name(struct proto *p);

static void
proto_enqueue(list *l, struct proto *p)
{
  add_tail(l, &p->n);
  p->last_state_change = now;
}

static void
proto_relink(struct proto *p)
{
  list *l;

  if (p->debug & D_STATES)
    {
      char *name = proto_state_name(p);
      if (name != p->last_state_name_announced)
	{
	  p->last_state_name_announced = name;
	  PD(p, "State changed to %s", proto_state_name(p));
	}
    }
  else
    p->last_state_name_announced = NULL;
  rem_node(&p->n);
  switch (p->core_state)
    {
    case FS_HAPPY:
      l = &active_proto_list;
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
  p->min_scope = SCOPE_SITE;
  p->hash_key = random_u32();
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
  rt_lock_table(p->table);
}

struct announce_hook *
proto_add_announce_hook(struct proto *p, struct rtable *t)
{
  struct announce_hook *h;

  if (!p->rt_notify)
    return NULL;
  DBG("Connecting protocol %s to table %s\n", p->name, t->name);
  PD(p, "Connected to table %s", t->name);
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
  c->name = pr->name;
  c->out_filter = FILTER_REJECT;
  c->table = c->global->master_rtc;
  c->debug = new_config->proto_default_debug;
  return c;
}

void
protos_preconfig(struct config *c)
{
  struct protocol *p;

  init_list(&c->protos);
  DBG("Protocol preconfig:");
  WALK_LIST(p, protocol_list)
    {
      DBG(" %s", p->name);
      p->name_counter = 0;
      if (p->preconfig)
	p->preconfig(p, c);
    }
  DBG("\n");
}

void
protos_postconfig(struct config *c)
{
  struct proto_config *x;
  struct protocol *p;

  DBG("Protocol postconfig:");
  WALK_LIST(x, c->protos)
    {
      DBG(" %s", x->name);
      p = x->protocol;
      if (p->postconfig)
	p->postconfig(x);
    }
  DBG("\n");
}

static struct proto *
proto_init(struct proto_config *c)
{
  struct protocol *p = c->protocol;
  struct proto *q = p->init(c);

  q->proto_state = PS_DOWN;
  q->core_state = FS_HUNGRY;
  proto_enqueue(&initial_proto_list, q);
  add_tail(&proto_list, &q->glob_node);
  PD(q, "Initializing%s", q->disabled ? " [disabled]" : "");
  return q;
}

void
protos_commit(struct config *new, struct config *old, int force_reconfig)
{
  struct proto_config *oc, *nc;
  struct proto *p, *n;

  DBG("protos_commit:\n");
  if (old)
    {
      WALK_LIST(oc, old->protos)
	{
	  struct proto *p = oc->proto;
	  struct symbol *sym = cf_find_symbol(oc->name);
	  if (sym && sym->class == SYM_PROTO && !new->shutdown)
	    {
	      /* Found match, let's check if we can smoothly switch to new configuration */
	      nc = sym->def;
	      if (!force_reconfig
		  && nc->protocol == oc->protocol
		  && nc->preference == oc->preference
		  && nc->disabled == oc->disabled
		  && nc->table->table == oc->table->table
		  && filter_same(nc->in_filter, oc->in_filter)
		  && filter_same(nc->out_filter, oc->out_filter)
		  && p->proto_state != PS_DOWN)
		{
		  /* Generic attributes match, try converting them and then ask the protocol */
		  p->debug = nc->debug;
		  if (p->proto->reconfigure && p->proto->reconfigure(p, nc))
		    {
		      DBG("\t%s: same\n", oc->name);
		      PD(p, "Reconfigured");
		      p->cf = nc;
		      p->name = nc->name;
		      p->in_filter = nc->in_filter;
		      p->out_filter = nc->out_filter;
		      nc->proto = p;
		      continue;
		    }
		}
	      /* Unsuccessful, force reconfig */
	      DBG("\t%s: power cycling\n", oc->name);
	      PD(p, "Reconfiguration failed, restarting");
	      p->cf_new = nc;
	      nc->proto = p;
	    }
	  else
	    {
	      DBG("\t%s: deleting\n", oc->name);
	      PD(p, "Unconfigured");
	      p->cf_new = NULL;
	    }
	  p->reconfiguring = 1;
	  config_add_obstacle(old);
	  proto_rethink_goal(p);
	}
    }

  WALK_LIST(nc, new->protos)
    if (!nc->proto)
      {
	DBG("\t%s: adding\n", nc->name);
	proto_init(nc);
      }
  DBG("\tdone\n");

  DBG("Protocol start\n");
  WALK_LIST_DELSAFE(p, n, initial_proto_list)
    proto_rethink_goal(p);
}

static void
proto_rethink_goal(struct proto *p)
{
  struct protocol *q;

  if (p->reconfiguring && p->core_state == FS_HUNGRY && p->proto_state == PS_DOWN)
    {
      struct proto_config *nc = p->cf_new;
      DBG("%s has shut down for reconfiguration\n", p->name);
      config_del_obstacle(p->cf->global);
      rem_node(&p->n);
      rem_node(&p->glob_node);
      mb_free(p);
      if (!nc)
	return;
      p = proto_init(nc);
    }

  /* Determine what state we want to reach */
  if (p->disabled || p->reconfiguring)
    {
      p->core_goal = FS_HUNGRY;
      if (p->core_state == FS_HUNGRY && p->proto_state == PS_DOWN)
	return;
    }
  else
    {
      p->core_goal = FS_HAPPY;
      if (p->core_state == FS_HAPPY && p->proto_state == PS_UP)
	return;
    }

  q = p->proto;
  if (p->core_goal == FS_HAPPY)		/* Going up */
    {
      if (p->core_state == FS_HUNGRY && p->proto_state == PS_DOWN)
	{
	  DBG("Kicking %s up\n", p->name);
	  PD(p, "Starting");
	  proto_init_instance(p);
	  proto_notify_state(p, (q->start ? q->start(p) : PS_UP));
	}
    }
  else 					/* Going down */
    {
      if (p->proto_state == PS_START || p->proto_state == PS_UP)
	{
	  DBG("Kicking %s down\n", p->name);
	  PD(p, "Shutting down");
	  proto_notify_state(p, (q->shutdown ? q->shutdown(p) : PS_DOWN));
	}
    }
}

void
protos_dump_all(void)
{
  struct proto *p;

  debug("Protocols:\n");

  WALK_LIST(p, active_proto_list)
    {
      debug("  protocol %s state %s/%s\n", p->name,
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
  WALK_LIST(p, flush_proto_list)
    debug("  flushing %s\n", p->name);
}

void
proto_build(struct protocol *p)
{
  add_tail(&protocol_list, &p->n);
  if (p->attr_class)
    {
      ASSERT(!attr_class_to_protocol[p->attr_class]);
      attr_class_to_protocol[p->attr_class] = p;
    }
}

void
protos_build(void)
{
  init_list(&protocol_list);
  init_list(&proto_list);
  init_list(&active_proto_list);
  init_list(&inactive_proto_list);
  init_list(&initial_proto_list);
  init_list(&flush_proto_list);
  proto_build(&proto_device);
#ifdef CONFIG_RIP
  proto_build(&proto_rip);
#endif
#ifdef CONFIG_STATIC
  proto_build(&proto_static);
#endif
#ifdef CONFIG_OSPF
  proto_build(&proto_ospf);
#endif
#ifdef CONFIG_PIPE
  proto_build(&proto_pipe);
#endif
#ifdef CONFIG_BGP
  proto_build(&proto_bgp);
#endif
  proto_pool = rp_new(&root_pool, "Protocols");
  proto_flush_event = ev_new(proto_pool);
  proto_flush_event->hook = proto_flush_all;
}

static void
proto_fell_down(struct proto *p)
{
  DBG("Protocol %s down\n", p->name);
  rt_unlock_table(p->table);
  proto_rethink_goal(p);
}

static void
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
	{
	  p->proto_state = ps;
	  proto_fell_down(p);
	  return;			/* The protocol might have ceased to exist */
	}
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
      cs = FS_FEEDING;
      p->attn->hook = proto_feed;
      ev_schedule(p->attn);
      break;
    case PS_STOP:
      if (ops != PS_DOWN)
	{
	schedule_flush:
	  DBG("%s: Scheduling flush\n", p->name);
	  proto_flush_hooks(p);
	  cs = FS_FLUSHING;
	  ev_schedule(proto_flush_event);
	}
      break;
    default:
      bug("Invalid state transition for %s from %s/%s to */%s", p->name, c_states[cs], p_states[ops], p_states[ps]);
    }
  p->proto_state = ps;
  p->core_state = cs;
  proto_relink(p);
}

static void
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
      WALK_PROTO_LIST(p)
	proto_do_show(p, verbose);
      WALK_PROTO_LIST_END;
    }
  cli_msg(0, "");
}

struct proto *
proto_get_named(struct symbol *sym, struct protocol *pr)
{
  struct proto *p, *q;

  if (sym)
    {
      if (sym->class != SYM_PROTO)
	cf_error("%s: Not a protocol", sym->name);
      p = ((struct proto_config *)sym->def)->proto;
      if (!p || p->proto != pr)
	cf_error("%s: Not a %s protocol", sym->name, pr->name);
    }
  else
    {
      p = NULL;
      WALK_LIST(q, active_proto_list)
	if (q->proto == pr)
	  {
	    if (p)
	      cf_error("There are multiple %s protocols running", pr->name);
	    p = q;
	  }
      if (!p)
	cf_error("There is no %s protocol running", pr->name);
    }
  return p;
}

void
proto_xxable(char *pattern, int xx)
{
  int cnt = 0;
  WALK_PROTO_LIST(p)
    if (patmatch(pattern, p->name))
      {
	cnt++;
	switch (xx)
	  {
	  case 0:
	    if (p->disabled)
	      cli_msg(-8, "%s: already disabled", p->name);
	    else
	      {
		cli_msg(-9, "%s: disabled", p->name);
		p->disabled = 1;
	      }
	    break;
	  case 1:
	    if (!p->disabled)
	      cli_msg(-10, "%s: already enabled", p->name);
	    else
	      {
		cli_msg(-11, "%s: enabled", p->name);
		p->disabled = 0;
	      }
	    break;
	  case 2:
	    if (p->disabled)
	      cli_msg(-8, "%s: already disabled", p->name);
	    else
	      {
		p->disabled = 1;
		proto_rethink_goal(p);
		p->disabled = 0;
		cli_msg(-12, "%s: restarted", p->name);
	      }
	    break;
	  default:
	    ASSERT(0);
	  }
	proto_rethink_goal(p);
      }
  WALK_PROTO_LIST_END;
  if (!cnt)
    cli_msg(8003, "No protocols match");
  else
    cli_msg(0, "");
}

void
proto_debug(char *pattern, unsigned int mask)
{
  int cnt = 0;
  WALK_PROTO_LIST(p)
    if (patmatch(pattern, p->name))
      {
	cnt++;
	p->debug = mask;
      }
  WALK_PROTO_LIST_END;
  if (!cnt)
    cli_msg(8003, "No protocols match");
  else
    cli_msg(0, "");
}
