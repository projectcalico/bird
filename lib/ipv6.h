/*
 *	BIRD -- IP Addresses et Cetera for IPv6
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_IPV4_H_
#define _BIRD_IPV4_H_

#include <netinet/in.h>
#include <string.h>

typedef struct ipv4_addr {
  u32 addr[4];
} ip_addr;

#define _MI(a,b,c,d) ((struct ip_addr) { a, b, c, d })
#define _I0(a) ((a).addr[0])
#define _I1(a) ((a).addr[1])
#define _I2(a) ((a).addr[2])
#define _I3(a) ((a).addr[3])

#define IPA_NONE _MI(0,0,0,0)

#define ipa_equal(x,y) (!memcmp(&(x),&(y),sizeof(ip_addr)))
#define ipa_and(a,b) _MI(_I0(a) & _I0(b), \
			 _I1(a) & _I1(b), \
			 _I2(a) & _I2(b), \
			 _I3(a) & _I3(b))
#define ipa_or(a,b) _MI(_I0(a) | _I0(b), \
			_I1(a) | _I1(b), \
			_I2(a) | _I2(b), \
			_I3(a) | _I3(b))
#define ipa_not(a) _MI(~_I0(a),~_I1(a),~_I2(a),~_I3(a))

#define ipa_mkmask(x) ipv6_mkmask(x)
#define ipa_mklen(x) ipv6_mklen(x)

ip_addr ipv6_mkmask(unsigned);
unsigned ipv6_mklen(ip_addr);

#endif
