/*
 *	Rest in pieces - RIP protocol
 *
 *	Copyright (c) 1999 Pavel Machek <pavel@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 *
 */

#define LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/protocol.h"
#include "nest/route.h"
#include "lib/socket.h"
#include "lib/resource.h"
#include "lib/lists.h"
#include "lib/timer.h"
#include "lib/md5.h"
#include "lib/string.h"

#include "rip.h"

#define P ((struct rip_proto *) p)
#define P_CF ((struct rip_proto_config *)p->cf)

#define PACKETLEN(num) (num * sizeof(struct rip_block) + sizeof(struct rip_packet_heading))

/* 1 == failed, 0 == ok */
int
rip_incoming_authentication( struct proto *p, struct rip_block_auth *block, struct rip_packet *packet, int num, ip_addr whotoldme )
{
  DBG( "Incoming authentication: " );
  switch (block->authtype) {	/* Authentication type */
  case AT_PLAINTEXT: 
    {
      struct password_item *passwd = get_best_password( P_CF->passwords, 0 );
      DBG( "Plaintext passwd" );
      if (!passwd) {
	log( L_AUTH "no passwords set and password authentication came\n" );
	return 1;
      }
      if (strncmp( (char *) (&block->packetlen), passwd->password, 16)) {
	log( L_AUTH "Passwd authentication failed!\n" );
	DBG( "Expected %s, got %s\n", passwd->password, &block->packetlen );
	return 1;
      }
    }
    return 0;
  case AT_MD5:
    DBG( "md5 password" );
    {
      struct password_item *head;
      struct rip_md5_tail *tail;

      if (block->packetlen != PACKETLEN(num)) {
	log( L_ERR "packetlen in md5 does not match computed value\n" );
	return 1;
      }

      tail = (struct rip_md5_tail *) ((char *) packet + (block->packetlen - sizeof(struct rip_block_auth)));
      if ((tail->mustbeFFFF != 0xffff) || (tail->mustbe0001 != 0x0001)) {
	log( L_ERR "md5 tail signature is not there\n" );
	return 1;
      }

      head = P_CF->passwords;
      while (head) {
	DBG( "time, " );
	if ((head->from > now) || (head->to < now))
	  goto skip;
	if (block->seq) {
	  struct neighbor *neigh = neigh_find(p, &whotoldme, 0);
	  if (!neigh) {
	    log( L_AUTH "Non-neighbour md5 checksummed packet?\n" );
	  } else {
	    if (neigh->aux > block->seq) {
	      log( L_AUTH "md5 prottected packet with lower numbers\n" );
	      return 0;
	    }
	    neigh->aux = block->seq;
	  }
	}
	DBG( "check, " );
	if (head->id == block->keyid) {
	  struct MD5Context ctxt;
	  char md5sum_packet[16];
	  char md5sum_computed[16];

	  memcpy(md5sum_packet, tail->md5, 16);
	  password_strncpy(tail->md5, head->password, 16);

	  MD5Init(&ctxt);
	  MD5Update(&ctxt, (char *) packet, block->packetlen );
	  MD5Final(md5sum_computed, &ctxt);

	  if (memcmp(md5sum_packet, md5sum_computed, 16))
	    return 1;
	  return 0;
	}
      skip:
	head = head->next;
      }
      return 1;
    }
  }
    
  return 0;
}

int
rip_outgoing_authentication( struct proto *p, struct rip_block_auth *block, struct rip_packet *packet, int num )
{
  struct password_item *passwd = get_best_password( P_CF->passwords, 0 );

  if (!P_CF->authtype)
    return PACKETLEN(num);

  DBG( "Outgoing authentication: " );

  if (!passwd) {
    log( L_ERR "no suitable password found for authentication\n" );
    return PACKETLEN(num);
  }

  block->authtype = P_CF->authtype;
  block->mustbeFFFF = 0xffff;
  switch (P_CF->authtype) {
  case AT_PLAINTEXT:
    password_strncpy( (char *) (&block->packetlen), passwd->password, 16);
    return PACKETLEN(num);
  case AT_MD5:
    {
      struct rip_md5_tail *tail;
      struct MD5Context ctxt;
      static int sequence = 0;

      if (num > PACKET_MD5_MAX)
	bug(  "we can not add MD5 authentication to this long packet\n" );

      block->keyid = passwd->id;
      block->authlen = 20;
      block->seq = sequence++;
      block->zero0 = 0;
      block->zero1 = 0;
      block->packetlen = PACKETLEN(num) + block->authlen;

      tail = (struct rip_md5_tail *) ((char *) packet + (block->packetlen - sizeof(struct rip_block_auth)));
      tail->mustbeFFFF = 0xffff;
      tail->mustbe0001 = 0x0001;
      password_strncpy( (char *) (&tail->md5), passwd->password, 16 );

      MD5Init(&ctxt);
      MD5Update(&ctxt, (char *) packet, block->packetlen );
      MD5Final((char *) (&tail->md5), &ctxt);
      return PACKETLEN(num) + block->authlen;
    }
  default:
    bug( "Uknown authtype in outgoing authentication?\n" );
  }
}
