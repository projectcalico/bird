/*
 *	BIRD Library -- IP address routines common for IPv4 and IPv6
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "nest/bird.h"
#include "lib/ip.h"

char *
ip_scope_text(unsigned scope)
{
  static char *scope_table[] = { "host", "link", "site", "org", "univ" };

  if (scope > SCOPE_UNIVERSE)
    return "?";
  else
    return scope_table[scope];
}
