/*
 *	BIRD -- Protocols
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "nest/bird.h"
#include "nest/protocol.h"
#include "lib/resource.h"
#include "lib/lists.h"

list proto_list;

void
protos_init(void)
{
  init_list(&proto_list);
}
