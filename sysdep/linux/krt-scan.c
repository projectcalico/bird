/*
 *	BIRD -- Linux Routing Table Scanning
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <string.h>

#define LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/route.h"
#include "nest/protocol.h"
#include "lib/timer.h"
#include "lib/unix.h"
#include "lib/krt.h"

#define SCANOPT struct krt_scan_params *p = &x->scanopt

static void
krt_scan_fire(timer *t)
{
  DBG("Scanning kernel table...\n");
}

void
krt_scan_preconfig(struct krt_proto *x)
{
  SCANOPT;

  p->recurrence = 10;			/* FIXME: use reasonable default value */
}

void
krt_scan_start(struct krt_proto *x)
{
  SCANOPT;
  timer *t = tm_new(x->p.pool);

  p->timer = t;
  t->hook = krt_scan_fire;
  t->data = x;
  t->recurrent = p->recurrence;
  krt_scan_fire(t);
  if (t->recurrent)
    tm_start(t, t->recurrent);
}

void
krt_scan_shutdown(struct krt_proto *x)
{
  SCANOPT;

  tm_stop(p->timer);
}
