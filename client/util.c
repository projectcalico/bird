/*
 *	BIRD Client -- Utility Functions
 *
 *	(c) 1999--2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "nest/bird.h"
#include "client/client.h"

/* Client versions of logging functions */

/* FIXME: Use bsprintf, so that %m works */

void
bug(char *msg, ...)
{
  va_list args;

  va_start(args, msg);
  cleanup();
  fputs("Internal error: ", stderr);
  vfprintf(stderr, msg, args);
  fputc('\n', stderr);
  exit(1);
}

void
die(char *msg, ...)
{
  va_list args;

  va_start(args, msg);
  cleanup();
  vfprintf(stderr, msg, args);
  fputc('\n', stderr);
  exit(1);
}
