/*
 *	BIRD -- IP Addresses et Cetera for IPv4
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_IPV4_H_
#define _BIRD_IPV4_H_

#include <netinet/in.h>

typedef struct ipv4_addr {
  u32 addr;
} ip_addr;

#define _I(x) (x).addr
#define _MI(x) ((struct ip_addr) { x })

#define IPA_NONE(_MI(0))

#define ipa_equal(x,y) (_I(x) == _I(y))
#define ipa_and(x,y) _MI(_I(x) & _I(y))
#define ipa_or(x,y) _MI(_I(x) | _I(y))
#define ipa_not(x) _MI(~_I(x))
#define ipa_mkmask(x) _MI(ipv4_mkmask(x))
#define ipa_mklen(x) ipv4_mklen(_I(x))

unsigned ipv4_mklen(u32);
u32 ipv4_mkmask(unsigned);

/* ??? htonl and ntohl ??? */

#endif
