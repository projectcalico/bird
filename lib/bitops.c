/*
 *	BIRD Library -- Generic Bit Operations
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "nest/bird.h"
#include "bitops.h"

u32
u32_mkmask(unsigned n)
{
  return n ? ~((1 << (32 - n)) - 1) : 0;
}

int
u32_masklen(u32 x)
{
  int l = 0;
  u32 n = ~x;

  if (n & (n+1)) return -1;
  if (x & 0x0000ffff) { x &= 0x0000ffff; l += 16; }
  if (x & 0x00ff00ff) { x &= 0x00ff00ff; l += 8; }
  if (x & 0x0f0f0f0f) { x &= 0x0f0f0f0f; l += 4; }
  if (x & 0x33333333) { x &= 0x33333333; l += 2; }
  if (x & 0x55555555) l++;
  if (x & 0xaaaaaaaa) l++;
  return l;
}
