/*
 *	BIRD Library -- IPv4 Address Manipulation Functions
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef IPV6

#include "nest/bird.h"
#include "lib/ip.h"

int
ipv4_classify(u32 a)
{
  u32 b = a >> 24U;

  if (b && b <= 0xdf)
    {
      if (b == 0x7f)
	return IADDR_HOST | SCOPE_HOST;
      else
	return IADDR_HOST | SCOPE_UNIVERSE;
    }
  if (b >= 0xe0 && b <= 0xef)
    return IADDR_MULTICAST | SCOPE_UNIVERSE;
  if (a == 0xffffffff)
    return IADDR_BROADCAST | SCOPE_LINK;
  return IADDR_INVALID;
}

#endif
