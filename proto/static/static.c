/*
 *	BIRD -- Static Route Generator
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
#include "conf/conf.h"

#include "static.h"

#define GET_DATA struct static_proto *p = (struct static_proto *) P

static void
static_start(struct proto *P)
{
  DBG("Static: take off!\n");
}

static void
static_neigh_notify(struct neighbor *n)
{
  DBG("Static: neighbor notify got, don't know why.\n");
}

static void
static_dump(struct proto *P)
{
  DBG("Static: no dumps available in demo version.\n");
}

void
static_init_instance(struct static_proto *P)
{
  struct proto *p = &P->p;

  p->preference = DEF_PREF_STATIC;
  p->start = static_start;
  p->neigh_notify = static_neigh_notify;
  p->dump = static_dump;
  /* FIXME: Should shutdown remove all routes? */
}

static void
static_init(struct protocol *p)
{
}

static void
static_preconfig(struct protocol *x)
{
}

static void
static_postconfig(struct protocol *p)
{
}

struct protocol proto_static = {
  { NULL, NULL },
  "Static",
  0,
  static_init,
  static_preconfig,
  static_postconfig
};
