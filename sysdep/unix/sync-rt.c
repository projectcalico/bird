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
#include "krt.h"

struct proto *cf_krt_proto;

void
krt_start(struct proto *P)
{
  struct krt_proto *p = (struct krt_proto *) P;
  krt_scan_start(p);
}

void
krt_shutdown(struct proto *P, int time)
{
  struct krt_proto *p = (struct krt_proto *) P;
  krt_scan_shutdown(p);
}

void
krt_preconfig(struct protocol *x)
{
  struct krt_proto *p = (struct krt_proto *) proto_new(&proto_unix_kernel, sizeof(struct krt_proto));

  cf_krt_proto = &p->p;
  p->p.preference = DEF_PREF_UKR;
  p->p.start = krt_start;
  p->p.shutdown = krt_shutdown;
  krt_scan_preconfig(p);
  krt_set_preconfig(p);
}

struct protocol proto_unix_kernel = {
  { NULL, NULL },
  "kernel",
  0,
  NULL,					/* init */
  krt_preconfig,
  NULL					/* postconfig */
};
