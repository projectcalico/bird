/*
 *	BIRD Internet Routing Daemon
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <nest/bird.h>
#include <lib/lists.h>

#include <nest/resource.h>

int
main(void)
{
	ip_addr x,y;

	x=y;
	if (ipa_equal(x,y)) return 1;

	return 0;
}
