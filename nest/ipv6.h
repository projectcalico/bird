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

#define ipa_equal(x,y) (!memcmp(&(x),&(y),sizeof(ip_addr)))

#endif
