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
#include "conf/conf.h"
#include "nest/route.h"
#include "nest/iface.h"

list protocol_list;
list proto_list;
list inactive_proto_list;

void *
proto_new(struct proto_config *c, unsigned size)
{
  struct protocol *pr = c->proto;
  struct proto *p = cfg_allocz(size);	/* FIXME: Allocate from global pool */

  p->cf = c;
  p->debug = c->debug;
  p->preference = c->preference;
  p->disabled = c->disabled;
  p->proto = pr;
  p->pool = rp_new(&root_pool, c->name);
  return p;
}

void *
proto_config_new(struct protocol *pr, unsigned size)
{
  struct proto_config *c = cfg_allocz(size);

  add_tail(&new_config->protos, &c->n);
  c->global = new_config;
  c->proto = pr;
  c->debug = pr->debug;
  c->name = pr->name;
  return c;
}

void
protos_preconfig(struct config *c)
{
  struct protocol *p;

  init_list(&proto_list);
  init_list(&inactive_proto_list);
  debug("Protocol preconfig:");
  WALK_LIST(p, protocol_list)
    {
      debug(" %s", p->name);
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
      p = x->proto;
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
      p = x->proto;
      q = p->init(x);
      add_tail(&inactive_proto_list, &q->n);
    }
  debug("\n");
}

static void
proto_start(struct proto *p)
{
  rem_node(&p->n);
  if (p->disabled)
    return;
  p->proto_state = PS_DOWN;
  p->core_state = FS_HUNGRY;
  if (p->proto->start && p->proto->start(p) != PS_UP)
    bug("Delayed protocol start not supported yet");
  p->proto_state = PS_UP;
  p->core_state = FS_FEEDING;
  if_feed_baby(p);
  rt_feed_baby(p);
  p->core_state = FS_HAPPY;
  add_tail(&proto_list, &p->n);
}

void
protos_start(void)
{
  struct proto *p, *n;

  debug("Protocol start\n");
  WALK_LIST_DELSAFE(p, n, inactive_proto_list)
    {
      debug("Starting %s\n", p->cf->name);
      proto_start(p);
    }
}

void
protos_dump_all(void)
{
  struct proto *p;
  static char *p_states[] = { "DOWN", "START", "UP", "STOP" };
  static char *c_states[] = { "HUNGRY", "FEEDING", "HAPPY", "FLUSHING" };

  debug("Protocols:\n");

  WALK_LIST(p, proto_list)
    {
      debug("  protocol %s: state %s/%s\n", p->cf->name, p_states[p->proto_state], c_states[p->core_state]);
      if (p->disabled)
	debug("\tDISABLED\n");
      else if (p->proto->dump)
	p->proto->dump(p);
    }
  WALK_LIST(p, inactive_proto_list)
    debug("  inactive %s\n", p->cf->name);
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
}
