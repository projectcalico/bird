/*
 *	BIRD -- Unix Interface Scanning and Syncing
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <string.h>
#include <sys/types.h>
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

int if_scan_sock = -1;

static void
scan_ifs(struct ifreq *r, int cnt)
{
  struct iface i, *pi;
  struct ifa a;
  char *err, *colon;
  unsigned fl;
  ip_addr netmask;
  int l;
  int sec = 0;

  if_start_update();
  for (cnt /= sizeof(struct ifreq); cnt; cnt--, r++)
    {
      bzero(&i, sizeof(i));
      bzero(&a, sizeof(a));
      DBG("%s\n", r->ifr_name);
      if (colon = strchr(r->ifr_name, ':'))
	{
	  /* It's an alias -- let's interpret it as a secondary interface address */
	  sec = 1;
	  *colon = 0;
	}
      strncpy(i.name, r->ifr_name, sizeof(i.name) - 1);
      get_sockaddr((struct sockaddr_in *) &r->ifr_addr, &a.ip, NULL);
      if (ipa_nonzero(a.ip))
	{
	  l = ipa_classify(a.ip);
	  if (l < 0 || !(l & IADDR_HOST))
	    {
	      log(L_ERR "%s: Invalid interface address", i.name);
	      a.ip = IPA_NONE;
	    }
	  else
	    {
	      a.scope = l & IADDR_SCOPE_MASK;
	      if (a.scope == SCOPE_HOST)
		i.flags |= IF_LOOPBACK | IF_IGNORE;
	    }
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
      a.pxlen = l;

      if (fl & IFF_POINTOPOINT)
	{
	  i.flags |= IF_UNNUMBERED;
	  a.pxlen = BITS_PER_IP_ADDRESS;
	  if (ioctl(if_scan_sock, SIOCGIFDSTADDR, r) < 0)
	    { err = "SIOCGIFDSTADDR"; goto faulty; }
	  get_sockaddr((struct sockaddr_in *) &r->ifr_addr, &a.opposite, NULL);
	}
      if (fl & IFF_LOOPBACK)
	i.flags |= IF_LOOPBACK | IF_IGNORE;
#ifndef CONFIG_ALL_MULTICAST
      if (fl & IFF_MULTICAST)
#endif
	i.flags |= IF_MULTICAST;

      a.prefix = ipa_and(a.ip, ipa_mkmask(a.pxlen));
      if (a.pxlen < 32)
	{
	  a.brd = ipa_or(a.prefix, ipa_not(ipa_mkmask(a.pxlen)));
	  if (ipa_equal(a.ip, a.prefix) || ipa_equal(a.ip, a.brd))
	    {
	      log(L_ERR "%s: Using network or broadcast address for interface", i.name);
	      goto bad;
	    }
	  if (fl & IFF_BROADCAST)
	    i.flags |= IF_BROADCAST;
	  if (a.pxlen < 30)
	    i.flags |= IF_MULTIACCESS;
	  else
	    a.opposite = ipa_opposite(a.ip);
	}
      else
	a.brd = a.opposite;

      if (ioctl(if_scan_sock, SIOCGIFMTU, r) < 0)
	{ err = "SIOCGIFMTU"; goto faulty; }
      i.mtu = r->ifr_mtu;

#ifdef SIOCGIFINDEX
      if (ioctl(if_scan_sock, SIOCGIFINDEX, r) < 0)
	DBG("SIOCGIFINDEX failed: %m\n");
      else
	i.index = r->ifr_ifindex;
#else
      /*
       *  The kernel doesn't give us real ifindices, but we still need them
       *  at least for OSPF unnumbered links. So let's make them up ourselves.
       */
      if (pi = if_find_by_name(i.name))
	i.index = pi->index;
      else
	{
	  static int if_index_counter = 1;
	  i.index = if_index_counter++;
	}
#endif

      pi = NULL;
      if (sec)
	{
	  a.flags |= IA_SECONDARY;
	  pi = if_find_by_index(i.index);
	}
      if (!pi)
	pi = if_update(&i);
      a.iface = pi;
      ifa_update(&a);
    }
  if_end_update();
}

void
krt_if_scan(struct kif_proto *p)
{
  struct ifconf ic;
  static int last_ifbuf_size = 4*sizeof(struct ifreq);
  int res;

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
	  if (res >= 0 && ic.ifc_len < last_ifbuf_size)
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
}

void
krt_if_preconfig(struct kif_config *c)
{
}

void
krt_if_start(struct kif_proto *p)
{
}

void
krt_if_shutdown(struct kif_proto *p)
{
}

void
krt_if_io_init(void)
{
  if_scan_sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
  DBG("Using socket %d for interface and route scanning\n", if_scan_sock);
  if (if_scan_sock < 0)
    die("Cannot create scanning socket: %m");
}
