/*
 *	BIRD -- Unix Kernel Route Syncer
 *
 *	(c) 1998--2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_KRT_SET_H_
#define _BIRD_KRT_SET_H_

struct krt_params {
};

struct krt_status {
};


static inline void krt_sys_init(struct krt_proto *p UNUSED) { }
static inline int krt_sys_reconfigure(struct krt_proto *p UNUSED, struct krt_config *n UNUSED, struct krt_config *o UNUSED) { return 1; }

static inline void krt_sys_preconfig(struct config *c UNUSED) { }
static inline void krt_sys_postconfig(struct krt_config *c UNUSED) { }
static inline void krt_sys_init_config(struct krt_config *c UNUSED) { }
static inline void krt_sys_copy_config(struct krt_config *d UNUSED, struct krt_config *s UNUSED) { }

#endif
