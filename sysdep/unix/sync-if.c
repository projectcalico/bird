/*
 *	BIRD -- Unix Interface Scanning and Syncing
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <errno.h>

#define LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/route.h"
#include "nest/protocol.h"
#include "lib/timer.h"
#include "lib/krt.h"

#include "unix.h"

int if_scan_sock;

static timer *if_scan_timer;

static void
scan_ifs(struct ifreq *r, int cnt)
{
  struct iface i;
  char *err;
  unsigned fl;
  ip_addr netmask;
  int l;

  for (cnt /= sizeof(struct ifreq); cnt; cnt--, r++)
    {
      bzero(&i, sizeof(i));
      DBG("%s\n", r->ifr_ifrn.ifrn_name);
      strncpy(i.name, r->ifr_ifrn.ifrn_name, sizeof(i.name) - 1);
      get_sockaddr((struct sockaddr_in *) &r->ifr_addr, &i.ip, NULL);
      if (ipa_nonzero(i.ip))
	{
	  l = ipa_classify(i.ip);
	  if (l < 0 || !(l & IADDR_HOST))
	    {
	      log(L_ERR "%s: Invalid interface address", i.name);
	      i.ip = IPA_NONE;
	    }
	  else if ((l & IADDR_SCOPE_MASK) == SCOPE_HOST)
	    i.flags |= IF_LOOPBACK | IF_IGNORE;
	}

      if (ioctl(if_scan_sock, SIOCGIFFLAGS, r) < 0)
	{
	  err = "SIOCGIFFLAGS";
	faulty:
	  log(L_ERR "%s(%s): %m", err, i.name);
	bad:
	  i.flags = (i.flags & ~IF_LINK_UP) | IF_ADMIN_DOWN;
	  continue;
	}
      fl = r->ifr_flags;
      if (fl & IFF_UP)
	i.flags |= IF_LINK_UP;

      if (ioctl(if_scan_sock, SIOCGIFNETMASK, r) < 0)
	{ err = "SIOCGIFNETMASK"; goto faulty; }
      get_sockaddr((struct sockaddr_in *) &r->ifr_addr, &netmask, NULL);
      l = ipa_mklen(netmask);
      if (l < 0 || l == 31)
	{
	  log(L_ERR "%s: Invalid netmask", i.name);
	  goto bad;
	}
      i.pxlen = l;

      if (fl & IFF_POINTOPOINT)
	{
	  i.flags |= IF_UNNUMBERED;
	  i.pxlen = BITS_PER_IP_ADDRESS;
	  if (ioctl(if_scan_sock, SIOCGIFDSTADDR, r) < 0)
	    { err = "SIOCGIFDSTADDR"; goto faulty; }
	  get_sockaddr((struct sockaddr_in *) &r->ifr_addr, &i.opposite, NULL);
	}
      if (fl & IFF_LOOPBACK)
	i.flags |= IF_LOOPBACK | IF_IGNORE;
#ifndef CONFIG_ALL_MULTICAST
      if (fl & IFF_MULTICAST)
#endif
	i.flags |= IF_MULTICAST;

      i.prefix = ipa_and(i.ip, ipa_mkmask(i.pxlen));
      if (i.pxlen < 32)
	{
	  i.brd = ipa_or(i.prefix, ipa_not(ipa_mkmask(i.pxlen)));
	  if (ipa_equal(i.ip, i.prefix) || ipa_equal(i.ip, i.brd))
	    {
	      log(L_ERR "%s: Using network or broadcast address for interface", i.name);
	      goto bad;
	    }
	  if (fl & IFF_BROADCAST)
	    i.flags |= IF_BROADCAST;
	  if (i.pxlen < 30)
	    i.flags |= IF_MULTIACCESS;
	  else
	    i.opposite = ipa_opposite(i.ip);
	}
      else
	i.brd = i.opposite;

      if (ioctl(if_scan_sock, SIOCGIFMTU, r) < 0)
	{ err = "SIOCGIFMTU"; goto faulty; }
      i.mtu = r->ifr_mtu;

#ifdef SIOCGIFINDEX
      if (ioctl(if_scan_sock, SIOCGIFINDEX, r) < 0)
	DBG("SIOCGIFINDEX failed: %m\n");
      else
	i.index = r->ifr_ifindex;
#else
      /* FIXME: What else? Guess ifindex (we need it at least for OSPF on unnumbered links)? */
#endif

      if_update(&i);
    }
  if_end_update();
}

static void
scan_if(timer *t)
{
  struct ifconf ic;
  static int last_ifbuf_size = 4*sizeof(struct ifreq);
  int res;
  struct krt_proto *p = t->data;

  DBG("It's interface scan time...\n");
  for(;;)
    {
      if (last_ifbuf_size)
	{
	  struct ifreq *r = alloca(last_ifbuf_size);
	  ic.ifc_ifcu.ifcu_req = r;
	  ic.ifc_len = last_ifbuf_size;
	  res = ioctl(if_scan_sock, SIOCGIFCONF, &ic);
	  if (res < 0 && errno != EFAULT)
	    die("SIOCCGIFCONF: %m");
	  if (res < last_ifbuf_size)
	    {
	      scan_ifs(r, ic.ifc_len);
	      break;
	    }
	}
#ifdef CLEAN_WAY_WORKING_ONLY_ON_LINUX_2_1	/* FIXME */
      ic.ifc_req = NULL;
      ic.ifc_len = 999999999;
      if (ioctl(if_scan_sock, SIOCGIFCONF, &ic) < 0)
	die("SIOCIFCONF: %m");
      ic.ifc_len += sizeof(struct ifreq);
      if (last_ifbuf_size < ic.ifc_len)
	{
	  last_ifbuf_size = ic.ifc_len;
	  DBG("Increased ifconf buffer size to %d\n", last_ifbuf_size);
	}
#else
      last_ifbuf_size *= 2;
      DBG("Increased ifconf buffer size to %d\n", last_ifbuf_size);
#endif
    }
  krt_scan_ifaces_done(p);
}

void
krt_if_start(struct krt_proto *p)
{
  struct krt_config *c = (struct krt_config *) p->p.cf;

  if_scan_timer = tm_new(p->p.pool);
  if_scan_timer->hook = scan_if;
  if_scan_timer->data = p;
  if_scan_timer->recurrent = c->ifopt.scan_time;
  scan_if(if_scan_timer);
  tm_start(if_scan_timer, c->ifopt.scan_time);
}

void
krt_if_preconfig(struct krt_config *c)
{
  c->ifopt.scan_time = 60;
}

void
krt_if_shutdown(struct krt_proto *p)
{
  tm_stop(if_scan_timer);
  /* FIXME: What should we do with interfaces? */
}

void
scan_if_init(void)
{
  if_scan_sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
  DBG("Using socket %d for interface and route scanning\n", if_scan_sock);
  if (if_scan_sock < 0)
    die("Cannot create scanning socket: %m");
}
