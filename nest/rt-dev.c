/*
 *	BIRD -- Direct Device Routes
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/protocol.h"
#include "nest/route.h"
#include "lib/resource.h"

static struct proto *dev_proto;

static void
dev_if_notify(struct proto *p, unsigned c, struct iface *old, struct iface *new)
{
  debug("IF notify %x\n", c);
}

static void
dev_start(struct proto *p)
{
}

static void
dev_init(struct protocol *p)
{
}

static void
dev_preconfig(struct protocol *x)
{
  struct proto *p = proto_new(&proto_device, sizeof(struct proto));

  dev_proto = p;
  p->preference = DEF_PREF_DIRECT;
  p->start = dev_start;
  p->if_notify = dev_if_notify;
}

static void
dev_postconfig(struct protocol *p)
{
}

struct protocol proto_device = {
  { NULL, NULL },
  "Device",
  0,
  dev_init,
  dev_preconfig,
  dev_postconfig
};
