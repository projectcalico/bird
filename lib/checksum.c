/*
 *	BIRD Library -- IP One-Complement Checksum
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdarg.h>

#include "nest/bird.h"
#include "checksum.h"

static u16
ipsum_calc(void *frag, unsigned len, va_list args)
{
  u16 sum = 0;

  for(;;)
    {
      u16 *x = frag;
      ASSERT(!(len % 2));
      while (len)
	{
	  u16 z = sum + *x++;
	  sum = z + (z < sum);
	  len -= 2;
	}
      frag = va_arg(args, void *);
      if (!frag)
	break;
      len = va_arg(args, unsigned);
    }
  return sum;
}

int
ipsum_verify(void *frag, unsigned len, ...)
{
  va_list args;
  u16 sum;

  va_start(args, len);
  sum = ipsum_calc(frag, len, args);
  va_end(args);
  return sum == 0xffff;
}

u16
ipsum_calculate(void *frag, unsigned len, ...)
{
  va_list args;
  u16 sum;

  va_start(args, len);
  sum = ipsum_calc(frag, len, args);
  va_end(args);
  return 0xffff - sum;
}
