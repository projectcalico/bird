/*
 *	BIRD -- Unix Routing Table Scanning and Syncing
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
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
#include "krt.h"

struct proto_config *cf_krt;

static int
krt_start(struct proto *p)
{
  struct krt_proto *k = (struct krt_proto *) p;

  krt_scan_start(k);
  krt_set_start(k);
  krt_if_start(k);
  return PS_UP;
}

int
krt_shutdown(struct proto *p)
{
  struct krt_proto *k = (struct krt_proto *) p;

  krt_scan_shutdown(k);
  krt_if_shutdown(k);
  krt_set_shutdown(k);
  return PS_DOWN;
}

static void
krt_preconfig(struct protocol *x, struct config *c)
{
  struct krt_config *z = proto_config_new(&proto_unix_kernel, sizeof(struct krt_config));

  cf_krt = &z->c;
  z->c.preference = DEF_PREF_UKR;
  krt_scan_preconfig(z);
  krt_set_preconfig(z);
  krt_if_preconfig(z);
}

static struct proto *
krt_init(struct proto_config *c)
{
  struct krt_proto *p = proto_new(c, sizeof(struct krt_proto));

  return &p->p;
}

struct protocol proto_unix_kernel = {
  name:		"Kernel",
  preconfig:	krt_preconfig,
  init:		krt_init,
  start:	krt_start,
  shutdown:	krt_shutdown,
};
