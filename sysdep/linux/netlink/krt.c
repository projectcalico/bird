/*
 *	BIRD -- Linux Netlink Interface
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/uio.h>

#define LOCAL_DEBUG

#undef ASYNC_NETLINK			/* Define if async notifications should be used (debug) */

#include "nest/bird.h"
#include "nest/route.h"
#include "nest/protocol.h"
#include "nest/iface.h"
#include "lib/timer.h"
#include "lib/unix.h"
#include "lib/krt.h"
#include "lib/socket.h"

/*
 *	We need to work around namespace conflicts between us and the kernel,
 *	but I prefer this way to being forced to rename our configuration symbols.
 *	This will disappear as soon as netlink headers become part of the libc.
 */

#undef CONFIG_NETLINK
#include <linux/config.h>
#ifndef CONFIG_NETLINK
#error "Kernel not configured to support netlink"
#endif

#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#ifndef MSG_TRUNC			/* FIXME: Hack to circumvent omissions in glibc includes */
#define MSG_TRUNC 0x20
#endif

struct proto_config *cf_krt;

/*
 *	Synchronous Netlink interface
 */

static int nl_sync_fd;			/* Unix socket for synchronous netlink actions */
static u32 nl_sync_seq;			/* Sequence number of last request sent */

static byte *nl_rx_buffer;		/* Receive buffer */
static int nl_rx_size = 8192;

static struct nlmsghdr *nl_last_hdr;	/* Recently received packet */
static unsigned int nl_last_size;

static void
nl_send(void *rq, int size)
{
  struct nlmsghdr *nh = rq;
  struct sockaddr_nl sa;

  memset(&sa, 0, sizeof(sa));
  sa.nl_family = AF_NETLINK;
  nh->nlmsg_len = size;
  nh->nlmsg_pid = 0;
  nh->nlmsg_seq = ++nl_sync_seq;
  if (sendto(nl_sync_fd, rq, size, 0, (struct sockaddr *)&sa, sizeof(sa)) < 0)
    die("rtnetlink sendto: %m");
  nl_last_hdr = NULL;
}

static void
nl_request_dump(int cmd)
{
  struct {
    struct nlmsghdr nh;
    struct rtgenmsg g;
  } req;
  req.nh.nlmsg_type = cmd;
  req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
  req.g.rtgen_family = PF_INET;
  nl_send(&req, sizeof(req));
}

static struct nlmsghdr *
nl_get_reply(void)
{
  for(;;)
    {
      if (!nl_last_hdr)
	{
	  struct iovec iov = { nl_rx_buffer, nl_rx_size };
	  struct sockaddr_nl sa;
	  struct msghdr m = { (struct sockaddr *) &sa, sizeof(sa), &iov, 1, NULL, 0, 0 };
	  int x = recvmsg(nl_sync_fd, &m, 0);
	  if (x < 0)
	    die("nl_get_reply: %m");
	  nl_last_size = x;
	  nl_last_hdr = (void *) nl_rx_buffer;
	  if (m.msg_flags & MSG_TRUNC)
	    bug("nl_get_reply: got truncated reply which should be impossible");
	}
      if (NLMSG_OK(nl_last_hdr, nl_last_size))
	{
	  struct nlmsghdr *h = nl_last_hdr;
	  if (h->nlmsg_seq != nl_sync_seq)
	    {
	      log(L_WARN "nl_get_reply: Ignoring out of sequence netlink packet (%x != %x)",
		  h->nlmsg_seq, nl_sync_seq);
	      continue;
	    }
	  nl_last_hdr = NLMSG_NEXT(h, nl_last_size);
	  return h;
	}
      if (nl_last_size)
	log(L_WARN "nl_get_reply: Found packet remnant of size %d", nl_last_size);
      nl_last_hdr = NULL;
    }
}

static char *
nl_error(struct nlmsghdr *h)
{
  struct nlmsgerr *e = NLMSG_DATA(h);
  if (h->nlmsg_len < NLMSG_LENGTH(sizeof(struct nlmsgerr)))
    return "Error message truncated";
  else
    return strerror(-e->error);
}

static struct nlmsghdr *
nl_get_scan(void)
{
  struct nlmsghdr *h = nl_get_reply();

  if (h->nlmsg_type == NLMSG_DONE)
    return NULL;
  if (h->nlmsg_type == NLMSG_ERROR)
    {
      log(L_ERR "Netlink error: %s", nl_error(h));
      return NULL;
    }
  return h;
}

/*
 *	Parsing of Netlink attributes
 */

static int nl_attr_len;

static void *
nl_checkin(struct nlmsghdr *h, int lsize)
{
  nl_attr_len = h->nlmsg_len - NLMSG_LENGTH(lsize);
  if (nl_attr_len < 0)
    {
      log(L_ERR "nl_checkin: underrun by %d bytes", -nl_attr_len);
      return NULL;
    }
  return NLMSG_DATA(h);
}

static int
nl_parse_attrs(struct rtattr *a, struct rtattr **k, int ksize)
{
  int max = ksize / sizeof(struct rtattr *);
  bzero(k, ksize);
  while (RTA_OK(a, nl_attr_len))
    {
      if (a->rta_type < max)
	k[a->rta_type] = a;
      a = RTA_NEXT(a, nl_attr_len);
    }
  if (nl_attr_len)
    {
      log(L_ERR "nl_parse_attrs: remnant of size %d", nl_attr_len);
      return 0;
    }
  else
    return 1;
}

/*
 *	Scanning of interfaces
 */

static void
nl_parse_link(struct nlmsghdr *h)
{
  struct ifinfomsg *i;
  struct rtattr *a[IFLA_STATS+1];

  if (!(i = nl_checkin(h, sizeof(*i))) || !nl_parse_attrs(IFLA_RTA(i), a, sizeof(a)))
    return;
  DBG("NEWLINK %d\n", i->ifi_index);
}

static void
nl_parse_addr(struct nlmsghdr *h)
{
  struct ifaddrmsg *i;
  struct rtattr *a[IFA_ANYCAST+1];

  if (!(i = nl_checkin(h, sizeof(*i))) || !nl_parse_attrs(IFA_RTA(i), a, sizeof(a)))
    return;
  DBG("NEWADDR %d\n", i->ifa_index);
}

static void
nl_scan_ifaces(void)
{
  struct nlmsghdr *h;

  nl_request_dump(RTM_GETLINK);
  while (h = nl_get_scan())
    if (h->nlmsg_type == RTM_NEWLINK || h->nlmsg_type == RTM_DELLINK)
      nl_parse_link(h);
    else
      log(L_DEBUG "nl_scan_ifaces: Unknown packet received (type=%d)", h->nlmsg_type);

  nl_request_dump(RTM_GETADDR);
  while (h = nl_get_scan())
    if (h->nlmsg_type == RTM_NEWADDR || h->nlmsg_type == RTM_DELADDR)
      nl_parse_addr(h);
    else
      log(L_DEBUG "nl_scan_ifaces: Unknown packet received (type=%d)", h->nlmsg_type);

  bug("NIIDW");
}

/*
 *	Asynchronous Netlink interface
 */

static sock *nl_async_sk;		/* BIRD socket for asynchronous notifications */

static int
nl_async_hook(sock *sk, int size)
{
  DBG("nl_async_hook\n");
  return 0;
}

/*
 *	Protocol core
 */

static void
krt_preconfig(struct protocol *x, struct config *c)
{
  struct krt_config *z = proto_config_new(&proto_unix_kernel, sizeof(struct krt_config));

  cf_krt = &z->c;
  z->c.preference = DEF_PREF_UKR;
}

static struct proto *
krt_init(struct proto_config *c)
{
  struct krt_proto *p = proto_new(c, sizeof(struct krt_proto));

  return &p->p;
}

static void
nl_open_async(struct proto *p)
{
  sock *sk;
  struct sockaddr_nl sa;

  sk = nl_async_sk = sk_new(p->pool);
  sk->type = SK_MAGIC;
  sk->rx_hook = nl_async_hook;
  sk->fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (sk->fd < 0 || sk_open(sk))
    die("Unable to open secondary rtnetlink socket: %m");

  bzero(&sa, sizeof(sa));
  sa.nl_family = AF_NETLINK;
  sa.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE;
  if (bind(sk->fd, (struct sockaddr *) &sa, sizeof(sa)) < 0)
    die("Unable to bind secondary rtnetlink socket: %m");
}

static int
krt_start(struct proto *p)
{
#ifdef ASYNC_NETLINK
  nl_open_async(p);
#endif

  /* FIXME: Filter kernel routing table etc. */

  return PS_UP;
}

static int
krt_shutdown(struct proto *p)
{
  /* FIXME: Remove all our routes from the kernel */

  return PS_DOWN;
}

void
scan_if_init(void)
{
  nl_sync_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if (nl_sync_fd < 0)
    die("Unable to open rtnetlink socket: %m");
  nl_sync_seq = now;
  nl_rx_buffer = xmalloc(nl_rx_size);
  /* FIXME: Should we fetch our local address and compare it with addresses of all incoming messages? */

  nl_scan_ifaces();
}

struct protocol proto_unix_kernel = {
  name:		"Kernel",
  preconfig:	krt_preconfig,
  init:		krt_init,
  start:	krt_start,
  shutdown:	krt_shutdown
};
