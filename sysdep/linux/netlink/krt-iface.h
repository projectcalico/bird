/*
 *	BIRD -- Unix Kernel Netlink Interface Syncer -- Dummy Include File
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_KRT_IFACE_H_
#define _BIRD_KRT_IFACE_H_

/*
 *  We don't have split iface/scan/set parts. See krt-scan.h.
 */

struct krt_if_params {
};

struct krt_if_status {
};

static inline void krt_if_construct(struct kif_config *c) { };
static inline void krt_if_shutdown(struct kif_proto *p) { };
static inline void krt_if_io_init(void) { };

#endif
