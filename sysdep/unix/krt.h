/*
 *	BIRD -- Unix Kernel Route Syncer
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_KRT_H_
#define _BIRD_KRT_H_

#include "lib/krt-scan.h"
#include "lib/krt-set.h"

/* sync-rt.c */

extern struct protocol proto_unix_kernel;

struct krt_proto {
  struct proto p;
  struct krt_set_params setopt;
  struct krt_scan_params scanopt;
};

extern struct proto *cf_krt_proto;

/* krt-scan.c */

void krt_scan_preconfig(struct krt_proto *);
void krt_scan_start(struct krt_proto *);
void krt_scan_shutdown(struct krt_proto *);

/* krt-set.c */

void krt_set_preconfig(struct krt_proto *);

#endif
