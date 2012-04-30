/*
 *	BIRD -- Linux Kernel Netlink Route Syncer -- Scanning
 *
 *	(c) 1998--2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_KRT_SCAN_H_
#define _BIRD_KRT_SCAN_H_

/*
 *  We don't have split iface/scan/set for Netlink. All options
 *  and run-time parameters are declared here instead of splitting
 *  to krt-set.h, krt-iface.h and this file.
 */

#define NL_NUM_TABLES 256

struct krt_params {
  int table_id;				/* Kernel table ID we sync with */
};

struct krt_status {
};


static inline void krt_sys_init(struct krt_proto *p UNUSED) { }

#endif
