/*
 *	BIRD -- Protocols
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include <string.h>

#include "nest/bird.h"
#include "nest/protocol.h"
#include "lib/resource.h"
#include "lib/lists.h"
#include "nest/confile.h"
#include "nest/route.h"
#include "nest/iface.h"

list protocol_list;
list proto_list;
list inactive_proto_list;

void *
proto_new(struct protocol *pr, unsigned size)
{
  struct proto *p = mp_alloc(cfg_mem, size);

  debug("proto_new(%s)\n", pr->name);
  bzero(p, sizeof(*p));
  p->proto = pr;
  p->name = pr->name;
  p->debug = pr->debug;
  p->pool = rp_new(&root_pool, pr->name);
  add_tail(&inactive_proto_list, &p->n);
  return p;
}

void
protos_preconfig(void)
{
  struct protocol *p;

  init_list(&proto_list);
  init_list(&inactive_proto_list);
  debug("Protocol preconfig\n");
  WALK_LIST(p, protocol_list)
    {
      debug("...%s\n", p->name);
      p->preconfig(p);
    }
}

void
protos_postconfig(void)
{
  struct protocol *p;

  debug("Protocol postconfig\n");
  WALK_LIST(p, protocol_list)
    {
      debug("...%s\n", p->name);
      p->postconfig(p);
    }
}

static void
proto_start(struct proto *p)
{
  rem_node(&p->n);
  if (p->start)
    p->start(p);
  if_feed_baby(p);
  rt_feed_baby(p);
  add_tail(&proto_list, &p->n);
}

void
protos_start(void)
{
  struct proto *p, *n;

  debug("Protocol start\n");
  WALK_LIST_DELSAFE(p, n, inactive_proto_list)
    {
      debug("...%s\n", p->name);
      proto_start(p);
    }
}

void
protos_dump_all(void)
{
  struct proto *p;

  debug("Protocols:\n");

  WALK_LIST(p, proto_list)
    {
      debug("  protocol %s:\n", p->name);
      if (p->dump)
	p->dump(p);
    }
  WALK_LIST(p, inactive_proto_list)
    debug("  inactive %s\n", p->name);
}

void
protos_build(void)
{
  init_list(&protocol_list);
  add_tail(&protocol_list, &proto_device.n);
  add_tail(&protocol_list, &proto_rip.n);
}

void
protos_init(void)
{
  struct protocol *p;

  debug("Initializing protocols\n");
  WALK_LIST(p, protocol_list)
    p->init(p);
}
