/*
 *	BIRD -- BGP Packet Processing
 *
 *	(c) 2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/protocol.h"
#include "nest/route.h"
#include "conf/conf.h"
#include "lib/unaligned.h"
#include "lib/socket.h"

#include "bgp.h"

static byte *
bgp_create_notification(struct bgp_conn *conn, byte *buf)
{
  DBG("BGP: Sending notification: code=%d, sub=%d, arg=%d:%d\n", conn->notify_code, conn->notify_subcode, conn->notify_arg, conn->notify_arg_size);
  buf[0] = conn->notify_code;
  buf[1] = conn->notify_subcode;
  switch (conn->notify_arg_size)
    {
    case 1:
      buf[2] = conn->notify_arg; return buf+3;
    case 2:
      put_u16(buf+2, conn->notify_arg); return buf+4;
    case 4:
      put_u32(buf+2, conn->notify_arg); return buf+6;
    default:
      bug("bgp_create_notification: unknown error code size");
    }
}

static byte *
bgp_create_open(struct bgp_conn *conn, byte *buf)
{
  DBG("BGP: Sending open\n");
  buf[0] = BGP_VERSION;
  put_u16(buf+1, conn->bgp->local_as);
  put_u16(buf+3, conn->bgp->cf->hold_time);
  put_u32(buf+5, conn->bgp->local_id);
  buf[9] = 0;				/* No optional parameters */
  return buf+10;
}

static byte *
bgp_create_update(struct bgp_conn *conn, byte *buf)
{
  DBG("BGP: Sending update\n");
  bug("Don't know how to create updates");
}

static void
bgp_create_header(byte *buf, unsigned int len, unsigned int type)
{
  memset(buf, 0xff, 16);		/* Marker */
  put_u16(buf+16, len);
  buf[18] = type;
}

static void
bgp_fire_tx(struct bgp_conn *conn)
{
  unsigned int s = conn->packets_to_send;
  sock *sk = conn->sk;
  byte *buf = sk->tbuf;
  byte *pkt = buf + BGP_HEADER_LENGTH;
  byte *end;
  int type;

  if (s & (1 << PKT_SCHEDULE_CLOSE))
    {
      conn->packets_to_send = 0;
      bug("Scheduled close");		/* FIXME */
    }
  if (s & (1 << PKT_NOTIFICATION))
    {
      s = 1 << PKT_SCHEDULE_CLOSE;
      type = PKT_NOTIFICATION;
      end = bgp_create_notification(conn, pkt);
    }
  else if (s & (1 << PKT_KEEPALIVE))
    {
      s &= ~(1 << PKT_KEEPALIVE);
      type = PKT_KEEPALIVE;
      end = pkt;			/* Keepalives carry no data */
      DBG("BGP: Sending keepalive\n");
    }
  else if (s & (1 << PKT_OPEN))
    {
      s &= ~(1 << PKT_OPEN);
      type = PKT_OPEN;
      end = bgp_create_open(conn, pkt);
    }
  else if (s & (1 << PKT_UPDATE))
    {
      end = bgp_create_update(conn, pkt);
      type = PKT_UPDATE;
      if (!end)
	{
	  conn->packets_to_send = 0;
	  return;
	}
    }
  else
    return;
  conn->packets_to_send = s;
  bgp_create_header(buf, end - buf, type);
  sk_send(sk, end - buf);
}

void
bgp_schedule_packet(struct bgp_conn *conn, int type)
{
  DBG("BGP: Scheduling packet type %d\n", type);
  conn->packets_to_send |= 1 << type;
  if (conn->sk->tpos != conn->sk->tbuf)
    bgp_fire_tx(conn);
}

void
bgp_tx(sock *sk)
{
  struct bgp_conn *conn = sk->data;

  DBG("BGP: TX hook\n");
  bgp_fire_tx(conn);
}

int
bgp_rx(sock *sk, int size)
{
  struct bgp_conn *conn = sk->data;
  byte *pkt_start = sk->rbuf;
  byte *end = pkt_start + size;

  DBG("BGP: RX hook: Got %d bytes\n", size);
  while (end >= pkt_start + BGP_HEADER_LENGTH)
    {
      if (conn->error_flag)
	return 1;
      bug("Incoming packets not handled"); /* FIXME */
    }
  if (pkt_start != sk->rbuf)
    {
      memmove(sk->rbuf, pkt_start, end - pkt_start);
      sk->rpos = sk->rbuf + (end - pkt_start);
    }
  return 0;
}
