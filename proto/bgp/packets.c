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
    case 0:	return buf + 1;
    case 1:	buf[2] = conn->notify_arg; return buf+3;
    case 2:	put_u16(buf+2, conn->notify_arg); return buf+4;
    case 4:	put_u32(buf+2, conn->notify_arg); return buf+6;
    default:	bug("bgp_create_notification: unknown error code size");
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

static unsigned int
bgp_encode_prefixes(struct bgp_proto *p, byte *w, struct bgp_bucket *buck, unsigned int remains)
{
  byte *start = w;
  ip_addr a;
  int bytes;

  while (!EMPTY_LIST(buck->prefixes) && remains >= 5)
    {
      struct bgp_prefix *px = SKIP_BACK(struct bgp_prefix, bucket_node, HEAD(buck->prefixes));
      DBG("\tDequeued route %I/%d\n", px->n.prefix, px->n.pxlen);
      *w++ = px->n.pxlen;
      bytes = (px->n.pxlen + 7) / 8;
      a = px->n.prefix;
      ipa_hton(a);
      memcpy(w, &a, bytes);
      w += bytes;
      rem_node(&px->bucket_node);
      fib_delete(&p->prefix_fib, px);
    }
  return w - start;
}

static byte *
bgp_create_update(struct bgp_conn *conn, byte *buf)
{
  struct bgp_proto *bgp = conn->bgp;
  struct bgp_bucket *buck;
  int remains = BGP_MAX_PACKET_LENGTH - BGP_HEADER_LENGTH - 4;
  byte *w, *wold;
  ip_addr ip;
  int wd_size = 0;
  int r_size = 0;

  DBG("BGP: Sending update\n");
  /* FIXME: Better timing of updates */
  w = buf+2;
  if ((buck = bgp->withdraw_bucket) && !EMPTY_LIST(buck->prefixes))
    {
      DBG("Withdrawn routes:\n");
      wd_size = bgp_encode_prefixes(bgp, w, buck, remains);
      w += wd_size;
      remains -= wd_size;
    }
  put_u16(buf, wd_size);

  if (remains >= 2048)
    {
      while ((buck = (struct bgp_bucket *) HEAD(bgp->bucket_queue))->send_node.next)
	{
	  if (EMPTY_LIST(buck->prefixes))
	    {
	      DBG("Deleting empty bucket %p\n", buck);
	      rem_node(&buck->send_node);
	      bgp_free_bucket(bgp, buck);
	      continue;
	    }
	  DBG("Processing bucket %p\n", buck);
	  w = bgp_encode_attrs(w, buck);
	  remains = BGP_MAX_PACKET_LENGTH - BGP_HEADER_LENGTH - (w-buf);
	  r_size = bgp_encode_prefixes(bgp, w, buck, remains);
	  w += r_size;
	  break;
	}
    }
  else
    {
      put_u16(w, 0);
      w += 2;
    }
  return (wd_size || r_size) ? w : NULL;
}

static void
bgp_create_header(byte *buf, unsigned int len, unsigned int type)
{
  memset(buf, 0xff, 16);		/* Marker */
  put_u16(buf+16, len);
  buf[18] = type;
}

static int
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
      bgp_close_conn(conn);
      return 0;
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
      bgp_start_timer(conn->keepalive_timer, conn->keepalive_time);
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
	  return 0;
	}
    }
  else
    return 0;
  conn->packets_to_send = s;
  bgp_create_header(buf, end - buf, type);
  return sk_send(sk, end - buf);
}

void
bgp_schedule_packet(struct bgp_conn *conn, int type)
{
  DBG("BGP: Scheduling packet type %d\n", type);
  conn->packets_to_send |= 1 << type;
  if (conn->sk->tpos == conn->sk->tbuf)
    while (bgp_fire_tx(conn))
      ;
}

void
bgp_tx(sock *sk)
{
  struct bgp_conn *conn = sk->data;

  DBG("BGP: TX hook\n");
  while (bgp_fire_tx(conn))
    ;
}

static void
bgp_rx_open(struct bgp_conn *conn, byte *pkt, int len)
{
  struct bgp_conn *other;
  struct bgp_proto *p = conn->bgp;
  struct bgp_config *cf = p->cf;
  unsigned as, hold;
  u32 id;

  /* Check state */
  if (conn->state != BS_OPENSENT)
    { bgp_error(conn, 5, 0, conn->state, 0); }

  /* Check message contents */
  if (len < 29 || len != 29 + pkt[28])
    { bgp_error(conn, 1, 2, len, 2); return; }
  if (pkt[19] != BGP_VERSION)
    { bgp_error(conn, 2, 1, pkt[19], 2); return; }
  as = get_u16(pkt+20);
  hold = get_u16(pkt+22);
  id = get_u32(pkt+24);
  DBG("BGP: OPEN as=%d hold=%d id=%08x\n", as, hold, id);
  if (cf->remote_as && as != p->remote_as)
    { bgp_error(conn, 2, 2, as, 0); return; }
  if (hold > 0 && hold < 3)
    { bgp_error(conn, 2, 6, hold, 0); return; }
  p->remote_id = id;
  if (pkt[28])				/* Currently we support no optional parameters */
    { bgp_error(conn, 2, 4, pkt[29], 0); return; }
  if (!id || id == 0xffffffff || id == p->local_id)
    { bgp_error(conn, 2, 3, id, 0); return; }

  /* Check the other connection */
  other = (conn == &p->outgoing_conn) ? &p->incoming_conn : &p->outgoing_conn;
  switch (other->state)
    {
    case BS_IDLE:
      break;
    case BS_CONNECT:
    case BS_ACTIVE:
    case BS_OPENSENT:
      DBG("BGP: Collision, closing the other connection\n");
      bgp_close_conn(other);
      break;
    case BS_OPENCONFIRM:
      if ((p->local_id < id) == (conn == &p->incoming_conn))
	{
	  /* Should close the other connection */
	  DBG("BGP: Collision, closing the other connection\n");
	  bgp_error(other, 6, 0, 0, 0);
	  break;
	}
      /* Fall thru */
    case BS_ESTABLISHED:
      /* Should close this connection */
      DBG("BGP: Collision, closing this connection\n");
      bgp_error(conn, 6, 0, 0, 0);
      return;
    default:
      bug("bgp_rx_open: Unknown state");
    }

  /* Make this connection primary */
  conn->primary = 1;
  p->conn = conn;

  /* Update our local variables */
  if (hold < p->cf->hold_time)
    conn->hold_time = hold;
  else
    conn->hold_time = p->cf->hold_time;
  conn->keepalive_time = p->cf->keepalive_time ? : conn->hold_time / 3;
  p->remote_as = as;
  p->remote_id = id;
  DBG("BGP: Hold timer set to %d, keepalive to %d, AS to %d, ID to %x\n", conn->hold_time, conn->keepalive_time, p->remote_as, p->remote_id);

  bgp_schedule_packet(conn, PKT_KEEPALIVE);
  bgp_start_timer(conn->hold_timer, conn->hold_time);
  conn->state = BS_OPENCONFIRM;
}

#define DECODE_PREFIX(pp, ll) do {		\
  int b = *pp++;				\
  int q;					\
  ll--;						\
  if (b > BITS_PER_IP_ADDRESS) { bgp_error(conn, 3, 10, b, 0); return; } \
  q = (b+7) / 8;				\
  if (ll < q) goto malformed;			\
  memcpy(&prefix, pp, q);			\
  pp += q;					\
  ll -= q;					\
  ipa_ntoh(prefix);				\
  prefix = ipa_and(prefix, ipa_mkmask(b));	\
  pxlen = b;					\
} while (0)
/* FIXME: Check validity of prefixes */

static void
bgp_rx_update(struct bgp_conn *conn, byte *pkt, int len)
{
  struct bgp_proto *bgp = conn->bgp;
  byte *withdrawn, *attrs, *nlri;
  ip_addr prefix;
  int withdrawn_len, attr_len, nlri_len, pxlen;
  net *n;
  rte e;
  rta *a = NULL;

  DBG("BGP: UPDATE\n");
  if (conn->state != BS_ESTABLISHED)
    { bgp_error(conn, 5, 0, conn->state, 0); return; }
  bgp_start_timer(conn->hold_timer, conn->hold_time);

  /* Find parts of the packet and check sizes */
  if (len < 23)
    {
      bgp_error(conn, 1, 2, len, 2);
      return;
    }
  withdrawn = pkt + 21;
  withdrawn_len = get_u16(pkt + 19);
  if (withdrawn_len + 23 > len)
    goto malformed;
  attrs = withdrawn + withdrawn_len + 2;
  attr_len = get_u16(attrs - 2);
  if (withdrawn_len + attr_len + 23 > len)
    goto malformed;
  nlri = attrs + attr_len;
  nlri_len = len - withdrawn_len - attr_len - 23;
  if (!attr_len && nlri_len)
    goto malformed;
  DBG("Sizes: withdrawn=%d, attrs=%d, NLRI=%d\n", withdrawn_len, attr_len, nlri_len);

  /* Withdraw routes */
  while (withdrawn_len)
    {
      DECODE_PREFIX(withdrawn, withdrawn_len);
      DBG("Withdraw %I/%d\n", prefix, pxlen);
      if (n = net_find(bgp->p.table, prefix, pxlen))
	rte_update(bgp->p.table, n, &bgp->p, NULL);
    }

  if (nlri_len)
    {
      a = bgp_decode_attrs(conn, attrs, attr_len, bgp_linpool);
      if (!a)
	return;
      while (nlri_len)
	{
	  rte *e;
	  DECODE_PREFIX(nlri, nlri_len);
	  DBG("Add %I/%d\n", prefix, pxlen);
	  e = rte_get_temp(rta_clone(a));
	  n = net_get(bgp->p.table, prefix, pxlen);
	  e->net = n;
	  e->pflags = 0;
	  rte_update(bgp->p.table, n, &bgp->p, e);
	}
      lp_flush(bgp_linpool);
      rta_free(a);
    }
  return;

malformed:
  if (a)
    rta_free(a);
  bgp_error(conn, 3, 1, len, 0);
}

static void
bgp_rx_notification(struct bgp_conn *conn, byte *pkt, int len)
{
  unsigned arg;

  if (len < 21)
    {
      bgp_error(conn, 1, 2, len, 2);
      return;
    }
  switch (len)
    {
    case 21: arg = 0; break;
    case 22: arg = pkt[21]; break;
    case 23: arg = get_u16(pkt+21); break;
    case 25: arg = get_u32(pkt+23); break;
    default: DBG("BGP: NOTIFICATION with too much data\n"); /* FIXME */ arg = 0;
    }
  DBG("BGP: NOTIFICATION %d.%d %08x\n", pkt[19], pkt[20], arg);	/* FIXME: Better reporting */
  conn->error_flag = 1;
  if (conn->primary)
    proto_notify_state(&conn->bgp->p, PS_STOP);
  bgp_schedule_packet(conn, PKT_SCHEDULE_CLOSE);
}

static void
bgp_rx_keepalive(struct bgp_conn *conn, byte *pkt, unsigned len)
{
  DBG("BGP: KEEPALIVE\n");
  bgp_start_timer(conn->hold_timer, conn->hold_time);
  switch (conn->state)
    {
    case BS_OPENCONFIRM:
      DBG("BGP: UP!!!\n");
      conn->state = BS_ESTABLISHED;
      bgp_attr_init(conn->bgp);
      proto_notify_state(&conn->bgp->p, PS_UP);
      break;
    case BS_ESTABLISHED:
      break;
    default:
      bgp_error(conn, 5, 0, conn->state, 0);
    }
}

static void
bgp_rx_packet(struct bgp_conn *conn, byte *pkt, unsigned len)
{
  DBG("BGP: Got packet %02x (%d bytes)\n", pkt[18], len);
  switch (pkt[18])
    {
    case PKT_OPEN:		return bgp_rx_open(conn, pkt, len);
    case PKT_UPDATE:		return bgp_rx_update(conn, pkt, len);
    case PKT_NOTIFICATION:      return bgp_rx_notification(conn, pkt, len);
    case PKT_KEEPALIVE:		return bgp_rx_keepalive(conn, pkt, len);
    default:			bgp_error(conn, 1, 3, pkt[18], 1);
    }
}

int
bgp_rx(sock *sk, int size)
{
  struct bgp_conn *conn = sk->data;
  byte *pkt_start = sk->rbuf;
  byte *end = pkt_start + size;
  unsigned i, len;

  DBG("BGP: RX hook: Got %d bytes\n", size);
  while (end >= pkt_start + BGP_HEADER_LENGTH)
    {
      if (conn->error_flag)
	{
	  DBG("BGP: Error, dropping input\n");
	  return 1;
	}
      for(i=0; i<16; i++)
	if (pkt_start[i] != 0xff)
	  {
	    bgp_error(conn, 1, 1, 0, 0);
	    break;
	  }
      len = get_u16(pkt_start+16);
      if (len < BGP_HEADER_LENGTH || len > BGP_MAX_PACKET_LENGTH)
	{
	  bgp_error(conn, 1, 2, len, 2);
	  break;
	}
      if (end < pkt_start + len)
	break;
      bgp_rx_packet(conn, pkt_start, len);
      pkt_start += len;
    }
  if (pkt_start != sk->rbuf)
    {
      memmove(sk->rbuf, pkt_start, end - pkt_start);
      sk->rpos = sk->rbuf + (end - pkt_start);
    }
  return 0;
}
