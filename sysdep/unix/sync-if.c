/*
 *	BIRD -- Unix Interface Scanning and Syncing
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
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
#include "lib/timer.h"

#include "unix.h"

int if_scan_sock;
int if_scan_period = 60;

static timer *if_scan_timer;

static void
scan_ifs(struct ifreq *r, int cnt)
{
  struct iface i;
  struct ifa a;
  char *err;
  unsigned fl;
  ip_addr netmask;
  int l;

  for (cnt /= sizeof(struct ifreq); cnt; cnt--, r++)
    {
      bzero(&i, sizeof(i));
      bzero(&a, sizeof(a));
      debug("%s\n", r->ifr_ifrn.ifrn_name);
      strncpy(i.name, r->ifr_ifrn.ifrn_name, sizeof(i.name) - 1);
      i.name[sizeof(i.name) - 1] = 0;
      i.ifa = &a;
      get_sockaddr((struct sockaddr_in *) &r->ifr_addr, &a.ip, NULL);
      l = ipa_classify(a.ip);
      if (l < 0 || !(l & IADDR_HOST))
	{
	  log(L_ERR "%s: Invalid interface address", i.name);
	  goto bad;
	}
      if ((l & IADDR_SCOPE_MASK) == SCOPE_HOST)
	i.flags |= IF_LOOPBACK | IF_IGNORE;

      if (ioctl(if_scan_sock, SIOCGIFFLAGS, r) < 0)
	{
	  err = "SIOCGIFFLAGS";
	faulty:
	  log(L_ERR "%s(%s): %m", err, i.name);
	bad:
	  i.flags = (i.flags & ~IF_UP) | IF_ADMIN_DOWN;
	  continue;
	}
      fl = r->ifr_flags;
      if (fl & IFF_UP)
	i.flags |= IF_UP;

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
	{ err = "SIOCGIFINDEX"; goto faulty; }
      i.index = r->ifr_ifindex;
#endif

      if_update(&i);
    }
}

static void
scan_if(timer *t)
{
  struct ifconf ic;
  static int last_ifbuf_size;
  int res;

  DBG("Scanning interfaces...\n");
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
      ic.ifc_ifcu.ifcu_req = NULL;
      if (ioctl(if_scan_sock, SIOCGIFCONF, &ic) < 0)
	die("SIOCIFCONF: %m");
      ic.ifc_len += sizeof(struct ifreq);
      if (last_ifbuf_size < ic.ifc_len)
	{
	  last_ifbuf_size = ic.ifc_len;
	  DBG("Increased ifconf buffer size to %d\n", last_ifbuf_size);
	}
    }
}

void
scan_if_init(void)
{
  if_scan_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  DBG("Using socket %d for interface and route scanning\n", if_scan_sock);
  if (if_scan_sock < 0)
    die("Cannot create scanning socket: %m");
  scan_if(NULL);
  if_scan_timer = tm_new(&root_pool);
  if_scan_timer->hook = scan_if;
  if_scan_timer->recurrent = if_scan_period;
  tm_start(if_scan_timer, if_scan_period);
}

