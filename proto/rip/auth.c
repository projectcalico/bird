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
#include "lib/md5.h"

#include "rip.h"

#define P ((struct rip_proto *) p)
#define P_CF ((struct rip_proto_config *)p->cf)

/* 1 == failed, 0 == ok */
int
rip_incoming_authentication( struct proto *p, struct rip_block_auth *block, struct rip_packet *packet, int num )
{
  DBG( "Incoming authentication: " );

  switch (block->authtype) {	/* Authentication type */
  case AT_PLAINTEXT:
    DBG( "Plaintext passwd" );
    if (!P_CF->passwords) {
      log( L_AUTH "no passwords set and password authentication came\n" );
      return 1;
    }
    if (strncmp( (char *) (&block->packetlen), P_CF->passwords->password, 16)) {
      log( L_AUTH, "Passwd authentication failed!\n" );
      return 1;
    }
    return 0;
  case AT_MD5:
    DBG( "md5 password" );
    {
      struct password_item *head;
      struct rip_md5_tail *tail;

      tail = (char *) packet + (block->packetlen - sizeof(struct rip_block_auth));

      head = P_CF->passwords;
      while (head) {
	if (head->id == block->keyid) {
	  struct MD5Context ctxt;
	  int i;
	  char md5sum_packet[16];
	  char md5sum_computed[16];

	  memcpy(md5sum_packet, tail->md5, i);
	  password_strncpy(tail->md5, head->password, 16);

	  MD5Init(&ctxt);
	  MD5Update(&ctxt, packet, block->packetlen );
	  MD5Final(md5sum_computed, &ctxt);

	  if (memcmp(md5sum_packet, md5sum_computed, 16))
	    return 1;
	}
	head = head->next;
      }
      return 1;
    }
  }
    
  return 0;
}

void
rip_outgoing_authentication( struct proto *p, struct rip_block_auth *block, struct rip_packet *packet, int num )
{
  DBG( "Outgoing authentication: " );

  block->authtype = P_CF->authtype;
  switch (P_CF->authtype) {
  case AT_PLAINTEXT:
    if (!P_CF->passwords) {
      log( L_ERR "no passwords set and password authentication requested\n" );
      return;
    }
    password_strncpy( (char *) (&block->packetlen), P_CF->passwords->password, 16);
    return;
  }
}
