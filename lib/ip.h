/*
 *	BIRD Internet Routing Daemon -- The Internet Protocol
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_IP_H_
#define _BIRD_IP_H_

#ifndef IPV6
#include "ipv4.h"
#else
#include "ipv6.h"
#endif

/*
 *	ip_classify() returns either a negative number for invalid addresses
 *	or scope OR'ed together with address type.
 */

#define IADDR_INVALID		-1
#define IADDR_SCOPE_MASK       	0xfff
#define IADDR_HOST		0x1000
#define IADDR_BROADCAST		0x2000
#define IADDR_MULTICAST		0x4000

/*
 *	Address scope
 */

#define SCOPE_HOST 0
#define SCOPE_LINK 1
#define SCOPE_SITE 2
#define SCOPE_UNIVERSE 3

/*
 *	Is it a valid network prefix?
 */

#define ip_is_prefix(a,l) (!ipa_nonzero(ipa_and(a, ipa_not(ipa_mkmask(l)))))

#endif
