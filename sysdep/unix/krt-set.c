/*
 *	BIRD -- Unix Routing Table Syncing
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <errno.h>

#define LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/route.h"
#include "nest/protocol.h"
#include "lib/timer.h"
#include "lib/unix.h"
#include "lib/krt.h"

void
krt_set_notify(struct proto *x, net *net, rte *new, rte *old)
{
  DBG("krt_set_notify(%I/%d)\n", net->n.prefix, net->n.pxlen);
}

void
krt_set_preconfig(struct krt_proto *x)
{
  x->p.rt_notify = krt_set_notify;
}
