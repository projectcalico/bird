/*
 *	BIRD -- Set/Community-list Operations
 *
 *	(c) 2000 Martin Mares <mj@ucw.cz>
 *	(c) 2000 Pavel Machek <pavel@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "nest/bird.h"
#include "nest/route.h"
#include "nest/attrs.h"
#include "lib/resource.h"
#include "lib/string.h"

void
int_set_format(struct adata *set, byte *buf, unsigned int size)
{
  u32 *z = (u32 *) set->data;
  int l = set->length / 4;
  int sp = 1;
  byte *end = buf + size - 16;

  while (l--)
    {
      if (sp)
	{
	  sp = 0;
	  *buf++ = ' ';
	}
      if (buf > end)
	{
	  strcpy(buf, "...");
	  return;
	}
      buf += bsprintf(buf, "%d:%d", *z/65536, *z & 0xffff);
      z++;
    }
  *buf = 0;
}
