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
#include "lib/krt-iface.h"

/* Flags stored in net->n.flags */

#define KRF_CREATE 0			/* Not seen in kernel table */
#define KRF_SEEN 1			/* Seen in kernel table during last scan */
#define KRF_UPDATE 2			/* Need to update this entry */
#define KRF_DELETE 3			/* Should be deleted */
#define KRF_LEARN 4			/* We should learn this route */

/* sync-rt.c */

extern struct protocol proto_unix_kernel;

struct krt_config {
  struct proto_config c;
  struct krt_set_params setopt;
  struct krt_scan_params scanopt;
  struct krt_if_params ifopt;
};

struct krt_proto {
  struct proto p;
  struct krt_set_status setstat;
  struct krt_scan_status scanstat;
  struct krt_if_status ifstat;
};

extern struct proto_config *cf_krt;

/* krt-scan.c */

void krt_scan_preconfig(struct krt_config *);
void krt_scan_start(struct krt_proto *);
void krt_scan_shutdown(struct krt_proto *);
void krt_scan_ifaces_done(struct krt_proto *);

/* krt-set.c */

void krt_set_preconfig(struct krt_config *);
void krt_set_start(struct krt_proto *);
void krt_set_shutdown(struct krt_proto *);

/* sync-if.c */

void krt_if_preconfig(struct krt_config *);
void krt_if_start(struct krt_proto *);
void krt_if_shutdown(struct krt_proto *);

#endif
