/*
 *	BIRD -- Unix Kernel Route Syncer -- Setting Parameters
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_KRT_SET_H_
#define _BIRD_KRT_SET_H_

struct krt_set_params {
};

struct krt_set_status {
};

void krt_remove_route(rte *old);
void krt_add_route(rte *new);
int krt_capable(rte *e);
void krt_set_notify(struct proto *x, net *net, rte *new, rte *old);

#endif
