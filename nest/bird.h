/*
 *	BIRD Internet Routing Daemon -- Basic Declarations
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_BIRD_H_
#define _BIRD_BIRD_H_

#include <sysdep/config.h>
#include <lib/birdlib.h>

#ifndef IPV6
#include <nest/ipv4.h>
#else
#include <nest/ipv6.h>
#endif

extern u32 router_id;			/* Our Router ID */
extern u16 this_as;			/* Our Autonomous System Number */

#endif
