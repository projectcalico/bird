/*
 *	BIRD -- Linux Netlink Interface
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/socket.h>

#include "nest/bird.h"
#include "nest/route.h"
#include "nest/protocol.h"
#include "nest/iface.h"
#include "lib/timer.h"
#include "lib/unix.h"
#include "lib/krt.h"

/*
 *	We need to work around namespace conflicts between us and the kernel,
 *	but I prefer this way to being forced to rename our configuration symbols.
 *	This will disappear as soon as netlink headers become part of the libc.
 */

#undef CONFIG_NETLINK
#include <linux/config.h>
#ifndef CONFIG_NETLINK
#error "Kernel not configured to support netlink"
#endif

#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#define LOCAL_DEBUG

struct proto_config *cf_krt;

static void
krt_preconfig(struct protocol *x, struct config *c)
{
  struct krt_config *z = proto_config_new(&proto_unix_kernel, sizeof(struct krt_config));

  cf_krt = &z->c;
  z->c.preference = DEF_PREF_UKR;
}

static struct proto *
krt_init(struct proto_config *c)
{
  struct krt_proto *p = proto_new(c, sizeof(struct krt_proto));

  return &p->p;
}

static int
krt_start(struct proto *p)
{
  /* FIXME: Filter kernel routing table etc. */

  return PS_UP;
}

static int
krt_shutdown(struct proto *p)
{
  /* FIXME: Remove all our routes from the kernel */

  return PS_DOWN;
}

void
scan_if_init(void)
{
  /* FIXME: What to do here? */
}

struct protocol proto_unix_kernel = {
  name:		"Kernel",
  preconfig:	krt_preconfig,
  init:		krt_init,
  start:	krt_start,
  shutdown:	krt_shutdown
};
