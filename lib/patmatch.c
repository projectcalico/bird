/*
 *	BIRD Library -- Generic Shell-Like Pattern Matching (currently only '?' and '*')
 *
 *	(c) 1998 Martin Mares, <mj@atrey.karlin.mff.cuni.cz>
 */

#include "nest/bird.h"
#include "lib/string.h"

#ifndef MATCH_FUNC_NAME
#define MATCH_FUNC_NAME patmatch
#endif

#ifndef Convert
#define Convert(x) x
#endif

int
MATCH_FUNC_NAME(byte *p, byte *s)
{
  while (*p)
    {
      if (*p == '?' && *s)
	p++, s++;
      else if (*p == '*')
	{
	  int z = p[1];

	  if (!z)
	    return 1;
	  if (z == '\\' && p[2])
	    z = p[2];
	  z = Convert(z);
	  for(;;)
	    {
	      while (*s && Convert(*s) != z)
		s++;
	      if (!*s)
		return 0;
	      if (MATCH_FUNC_NAME(p+1, s))
		return 1;
	      s++;
	    }
	}
      else
	{
	  if (*p == '\\' && p[1])
	    p++;
	  if (Convert(*p++) != Convert(*s++))
	    return 0;
	}
    }
  return !*s;
}
