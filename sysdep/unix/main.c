/*
 *	BIRD Internet Routing Daemon -- Unix Entry Point
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>

#include "nest/bird.h"
#include "lib/lists.h"
#include "lib/resource.h"
#include "nest/route.h"

int
main(void)
{
  log(L_INFO "Launching BIRD -1.-1-pre-omega...");

  log_init_debug(NULL);
  resource_init();

  return 0;
}
