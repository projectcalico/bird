/*
 *	BIRD Internet Routing Daemon
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "nest/bird.h"
#include "lib/lists.h"
#include "lib/resource.h"

int
main(void)
{
  log_init_debug(NULL);
  resource_init();

  return 0;
}
