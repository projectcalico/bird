/*
 *	BIRD -- Path Operations
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
#include "lib/unaligned.h"
#include "lib/string.h"

struct adata *
as_path_prepend(struct linpool *pool, struct adata *olda, int as)
{
  struct adata *newa;

  if (olda->length && olda->data[0] == 2 && olda->data[1] < 255) /* Starting with sequence => just prepend the AS number */
    {
      newa = lp_alloc(pool, sizeof(struct adata) + olda->length + 2);
      newa->length = olda->length + 2;
      newa->data[0] = 2;
      newa->data[1] = olda->data[1] + 1;
      memcpy(newa->data+4, olda->data+2, olda->length-2);
    }
  else					/* Create new path segment */
    {
      newa = lp_alloc(pool, sizeof(struct adata) + olda->length + 4);
      newa->length = olda->length + 4;
      newa->data[0] = 2;
      newa->data[1] = 1;
      memcpy(newa->data+4, olda->data, olda->length);
    }
  put_u16(newa->data+2, as);
  return newa;
}

void
as_path_format(struct adata *path, byte *buf, unsigned int size)
{
  byte *p = path->data;
  byte *e = p + path->length - 8;
  byte *end = buf + size;
  int sp = 1;
  int l, type, isset, as;

  while (p < e)
    {
      if (buf > end)
	{
	  strcpy(buf, " ...");
	  return;
	}
      isset = (*p++ == AS_PATH_SET);
      l = *p++;
      if (isset)
	{
	  if (!sp)
	    *buf++ = ' ';
	  *buf++ = '{';
	  sp = 0;
	}
      while (l-- && buf <= end)
	{
	  if (!sp)
	    *buf++ = ' ';
	  buf += bsprintf(buf, "%d", get_u16(p));
	  p += 2;
	  sp = 0;
	}
      if (isset)
	{
	  *buf++ = ' ';
	  *buf++ = '}';
	  sp = 0;
	}
    }
  *buf = 0;
}
