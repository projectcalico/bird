/*
 *	BIRD -- Unix Kernel Route Syncer -- Setting Parameters
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_KRT_SET_H_
#define _BIRD_KRT_SET_H_

struct krt_set_params {
};

void krt_remove_route(net *net, rte *old);
void krt_add_route(net *net, rte *new);
int krt_capable(net *net, rte *e);

#endif
