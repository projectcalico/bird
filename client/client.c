/*
 *	BIRD Client
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "nest/bird.h"
#include "client/client.h"

int
main(int argc, char **argv)
{
  return client_main(argc, argv);	/* Call sysdep code */
}
