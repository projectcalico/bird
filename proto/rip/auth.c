/*
 *	Rest in pieces - RIP protocol
 *
 *	Copyright (c) 1999 Pavel Machek <pavel@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include <string.h>
#include <stdlib.h>

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/protocol.h"
#include "nest/route.h"
#include "lib/socket.h"
#include "lib/resource.h"
#include "lib/lists.h"
#include "lib/timer.h"

#include "rip.h"

#define P ((struct rip_proto *) p)
#define P_CF ((struct rip_proto_config *)p->cf)

int
rip_incoming_authentication( struct proto *p, struct rip_block *block, struct rip_packet *packet, int num )
{
  DBG( "Incoming authentication: " );

  switch (block->tag) {	/* Authentication type */
  case AT_PLAINTEXT:
    DBG( "Plaintext passwd" );
    if (strncmp( (char *) (&block->network), P_CF->password, 16)) {
      log( L_AUTH, "Passwd authentication failed!\n" );
      return 1;
    }
    return 0;
  }
    
  return 0;
}

void
rip_outgoing_authentication( struct proto *p, struct rip_block *block, struct rip_packet *packet, int num )
{
  DBG( "Outgoing authentication: " );

  block->tag = P_CF->authtype;
  switch (P_CF->authtype) {
  case AT_PLAINTEXT:
    strncpy( (char *) (&block->network), P_CF->password, 16);
    return;
  }
}
