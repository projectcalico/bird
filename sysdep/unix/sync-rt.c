/*
 *	BIRD -- Unix Routing Table Scanning and Syncing
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

#include "unix.h"

void
uk_rt_notify(struct proto *p, net *net, rte *new, rte *old)
{
}

void
uk_start(struct proto *p)
{
}

void
uk_init(struct protocol *x)
{
}

void
uk_preconfig(struct protocol *x)
{
  struct proto *p = proto_new(&proto_unix_kernel, sizeof(struct proto));

  p->preference = DEF_PREF_UKR;
  p->rt_notify = uk_rt_notify;
  p->start = uk_start;
}

void
uk_postconfig(struct protocol *x)
{
}

struct protocol proto_unix_kernel = {
  { NULL, NULL },
  "kernel",
  0,
  uk_init,
  uk_preconfig,
  uk_postconfig
};
