/*
 *	BIRD Library -- malloc() With Checking
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdlib.h>

#include "nest/bird.h"
#include "lib/resource.h"

void *
xmalloc(unsigned size)
{
  void *p = malloc(size);
  if (p)
    return p;
  die("Unable to allocate %d bytes of memory", size);
}
