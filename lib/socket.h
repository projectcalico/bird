/*
 *	BIRD Socket Interface
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_SOCKET_H_
#define _BIRD_SOCKET_H_

#include "lib/resource.h"

typedef struct birdsock {
  resource r;
  int type;				/* Socket type */
  void *data;				/* User data */
  ip_addr saddr, daddr;			/* IPA_NONE = unspecified */
  unsigned sport, dport;		/* 0 = unspecified (for IP: protocol type) */
  int tos;				/* TOS and priority, -1 = default */
  int ttl;				/* Time To Live, -1 = default */
  struct iface *iface;			/* Bound to interface */

  byte *rbuf, *rpos;			/* NULL=allocate automatically */
  unsigned rbsize;
  void (*rx_hook)(struct birdsock *);	/* NULL=receiving turned off */

  byte *tbuf, *tpos;			/* NULL=allocate automatically */
  byte *ttx;				/* Internal */
  unsigned tbsize;
  void (*tx_hook)(struct birdsock *);

  void (*err_hook)(struct birdsock *, int);
} socket;

socket *sk_get(pool *);			/* Allocate new socket */
int sk_open(socket *);			/* Open socket */
int sk_send(socket *);			/* Try to send queued data, > 0 if succeeded */
int sk_send_to(socket *, ip_addr, unsigned); /* Send queued data to given destination */

/*
 *	Socket types		     SA SP DA DP IF  SendTo	(?=may, -=must not, *=must)
 */

#define SK_TCP_PASSIVE	0	   /* ?  *  -  -  -  -		*/
#define SK_TCP_ACTIVE	1          /* ?  ?  *  *  -  -		*/
#define SK_UDP		2          /* ?  ?  -  -  -  ?		*/
#define SK_UDP_MC       3          /* ?  ?  *  *  *  -		*/
#define SK_IP		4          /* ?  ?  -  *  -  ?		*/
#define SK_IP_MC	5          /* ?  ?  *  *  *  -		*/

#endif
