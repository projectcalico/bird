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

#include "lib/bitops.h"

#ifdef DEBUG

/*
 *	Use the structural representation when you want to make sure
 *	nobody unauthorized attempts to handle ip_addr as number.
 */

typedef struct ipv4_addr {
  u32 addr;
} ip_addr;

#define _I(x) (x).addr
#define _MI(x) ((struct ipv4_addr) { x })

#else

typedef u32 ip_addr;

#define _I(x) (x)
#define _MI(x) (x)

#endif

#define BITS_PER_IP_ADDRESS 32

#define IPA_NONE (_MI(0))

#define ipa_equal(x,y) (_I(x) == _I(y))
#define ipa_nonzero(x) _I(x)
#define ipa_and(x,y) _MI(_I(x) & _I(y))
#define ipa_or(x,y) _MI(_I(x) | _I(y))
#define ipa_not(x) _MI(~_I(x))
#define ipa_mkmask(x) _MI(u32_mkmask(x))
#define ipa_mklen(x) u32_masklen(_I(x))
#define ipa_hash(x) ipv4_hash(_I(x))
#define ipa_hton(x) x = _MI(htonl(_I(x)))
#define ipa_ntoh(x) x = _MI(ntohl(_I(x)))
#define ipa_classify(x) ipv4_classify(_I(x))

int ipv4_classify(u32);

/* FIXME: Is this hash function uniformly distributed over standard routing tables? */
static inline unsigned ipv4_hash(u32 a)
{
  return a ^ (a >> 16) ^ (a >> 24);
}

#endif
