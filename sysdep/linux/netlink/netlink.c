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
#include <net/if.h>
#include <sys/socket.h>
#include <sys/uio.h>

#define LOCAL_DEBUG

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

/*
 *	Synchronous Netlink interface
 */

static int nl_sync_fd = -1;		/* Unix socket for synchronous netlink actions */
static u32 nl_sync_seq;			/* Sequence number of last request sent */

static byte *nl_rx_buffer;		/* Receive buffer */
static int nl_rx_size = 8192;

static struct nlmsghdr *nl_last_hdr;	/* Recently received packet */
static unsigned int nl_last_size;

static void
nl_open(void)
{
  if (nl_sync_fd < 0)
    {
      nl_sync_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
      if (nl_sync_fd < 0)
	die("Unable to open rtnetlink socket: %m");
      nl_sync_seq = now;
      nl_rx_buffer = xmalloc(nl_rx_size);
    }
}

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
	  if (sa.nl_pid)		/* It isn't from the kernel */
	    {
	      DBG("Non-kernel packet\n");
	      continue;
	    }
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
nl_parse_link(struct nlmsghdr *h, int scan)
{
  struct ifinfomsg *i;
  struct rtattr *a[IFLA_STATS+1];
  int new = h->nlmsg_type == RTM_NEWLINK;
  struct iface f;
  struct iface *ifi;
  char *name;
  u32 mtu;
  unsigned int fl;

  if (!(i = nl_checkin(h, sizeof(*i))) || !nl_parse_attrs(IFLA_RTA(i), a, sizeof(a)))
    return;
  if (!a[IFLA_IFNAME] || RTA_PAYLOAD(a[IFLA_IFNAME]) < 2 ||
      !a[IFLA_MTU] || RTA_PAYLOAD(a[IFLA_MTU]) != 4)
    {
      log(L_ERR "nl_parse_link: Malformed message received");
      return;
    }
  name = RTA_DATA(a[IFLA_IFNAME]);
  memcpy(&mtu, RTA_DATA(a[IFLA_MTU]), sizeof(u32));

  ifi = if_find_by_index(i->ifi_index);
  if (!new)
    {
      DBG("KRT: IF%d(%s) goes down\n", i->ifi_index, name);
      if (ifi && !scan)
	{
	  memcpy(&f, ifi, sizeof(struct iface));
	  f.flags |= IF_ADMIN_DOWN;
	  if_update(&f);
	}
    }
  else
    {
      DBG("KRT: IF%d(%s) goes up (mtu=%d,flg=%x)\n", i->ifi_index, name, mtu, i->ifi_flags);
      if (ifi)
	memcpy(&f, ifi, sizeof(f));
      else
	{
	  bzero(&f, sizeof(f));
	  f.index = i->ifi_index;
	}
      strncpy(f.name, RTA_DATA(a[IFLA_IFNAME]), sizeof(f.name)-1);
      f.mtu = mtu;
      f.flags = 0;
      fl = i->ifi_flags;
      if (fl & IFF_UP)
	f.flags |= IF_LINK_UP;
      if (fl & IFF_POINTOPOINT)
	f.flags |= IF_UNNUMBERED | IF_MULTICAST;
      if (fl & IFF_LOOPBACK)
	f.flags |= IF_LOOPBACK | IF_IGNORE;
      if (fl & IFF_BROADCAST)
	f.flags |= IF_BROADCAST | IF_MULTICAST;
      if_update(&f);
    }
}

static void
nl_parse_addr(struct nlmsghdr *h)
{
  struct ifaddrmsg *i;
  struct rtattr *a[IFA_ANYCAST+1];
  int new = h->nlmsg_type == RTM_NEWADDR;
  struct iface f;
  struct iface *ifi;

  if (!(i = nl_checkin(h, sizeof(*i))) || !nl_parse_attrs(IFA_RTA(i), a, sizeof(a)))
    return;
  if (i->ifa_family != AF_INET)
    return;
  if (!a[IFA_ADDRESS] || RTA_PAYLOAD(a[IFA_ADDRESS]) != 4 ||
      !a[IFA_LOCAL] || RTA_PAYLOAD(a[IFA_LOCAL]) != 4 ||
      (a[IFA_BROADCAST] && RTA_PAYLOAD(a[IFA_BROADCAST]) != 4))
    {
      log(L_ERR "nl_parse_addr: Malformed message received");
      return;
    }
  if (i->ifa_flags & IFA_F_SECONDARY)
    {
      DBG("KRT: Received address message for secondary address which is not supported.\n"); /* FIXME */
      return;
    }

  ifi = if_find_by_index(i->ifa_index);
  if (!ifi)
    {
      log(L_ERR "KRT: Received address message for unknown interface %d\n", i->ifa_index);
      return;
    }
  memcpy(&f, ifi, sizeof(f));

  if (i->ifa_prefixlen > 32 || i->ifa_prefixlen == 31 ||
      (f.flags & IF_UNNUMBERED) && i->ifa_prefixlen != 32)
    {
      log(L_ERR "KRT: Invalid prefix length for interface %s: %d\n", f.name, i->ifa_prefixlen);
      new = 0;
    }

  f.ip = f.brd = f.opposite = IPA_NONE;
  if (!new)
    {
      DBG("KRT: IF%d IP address deleted\n");
      f.pxlen = 0;
    }
  else
    {
      memcpy(&f.ip, RTA_DATA(a[IFA_LOCAL]), sizeof(f.ip));
      f.ip = ipa_ntoh(f.ip);
      f.pxlen = i->ifa_prefixlen;
      if (f.flags & IF_UNNUMBERED)
	{
	  memcpy(&f.opposite, RTA_DATA(a[IFA_ADDRESS]), sizeof(f.opposite));
	  f.opposite = f.brd = ipa_ntoh(f.opposite);
	}
      else if ((f.flags & IF_BROADCAST) && a[IFA_BROADCAST])
	{
	  memcpy(&f.brd, RTA_DATA(a[IFA_BROADCAST]), sizeof(f.brd));
	  f.brd = ipa_ntoh(f.brd);
	}
      /* else a NBMA link */
      f.prefix = ipa_and(f.ip, ipa_mkmask(f.pxlen));
      DBG("KRT: IF%d IP address set to %I, net %I/%d, brd %I, opp %I\n", f.index, f.ip, f.prefix, f.pxlen, f.brd, f.opposite);
    }
  if_update(&f);
}

void
krt_if_scan(struct krt_proto *p)
{
  struct nlmsghdr *h;

  if_start_update();

  nl_request_dump(RTM_GETLINK);
  while (h = nl_get_scan())
    if (h->nlmsg_type == RTM_NEWLINK || h->nlmsg_type == RTM_DELLINK)
      nl_parse_link(h, 1);
    else
      log(L_DEBUG "nl_scan_ifaces: Unknown packet received (type=%d)", h->nlmsg_type);

  nl_request_dump(RTM_GETADDR);
  while (h = nl_get_scan())
    if (h->nlmsg_type == RTM_NEWADDR || h->nlmsg_type == RTM_DELADDR)
      nl_parse_addr(h);
    else
      log(L_DEBUG "nl_scan_ifaces: Unknown packet received (type=%d)", h->nlmsg_type);

  if_end_update();
}

/*
 *	Routes
 */

int
krt_capable(rte *e)
{
  return 1;	/* FIXME */
}

void
krt_set_notify(struct proto *p, net *n, rte *new, rte *old)
{
  /* FIXME */
}

void
krt_scan_fire(struct krt_proto *p)
{
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

static void
nl_open_async(struct krt_proto *p)
{
  sock *sk;
  struct sockaddr_nl sa;

  DBG("KRT: Opening async netlink socket\n");

  sk = nl_async_sk = sk_new(p->p.pool);
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

/*
 *	Interface to the UNIX krt module
 */

void
krt_scan_preconfig(struct krt_config *x)
{
  x->scan.async = 1;
}

void
krt_scan_start(struct krt_proto *p)
{
  nl_open();
  if (KRT_CF->scan.async)
    nl_open_async(p);
}

void
krt_scan_shutdown(struct krt_proto *p)
{
}
