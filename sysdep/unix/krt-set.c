/*
 *	BIRD -- Unix Routing Table Syncing
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <net/route.h>

#define LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/route.h"
#include "nest/protocol.h"
#include "lib/unix.h"
#include "lib/krt.h"

int
krt_capable(net *net, rte *e)
{
  rta *a = e->attrs;

  return
    a->cast == RTC_UNICAST &&
    (a->dest == RTD_ROUTER
#ifndef CONFIG_AUTO_ROUTES
     || a->dest == RTD_DEVICE
#endif
#ifdef RTF_REJECT
     || a->dest == RTD_UNREACHABLE
#endif
     ) &&
    !a->tos;
}

void
krt_remove_route(net *net, rte *old)
{
  struct rtentry re;

  if (old && !krt_capable(net, old))
    {
      DBG("krt_remove_route(ignored %I/%d)\n", net->n.prefix, net->n.pxlen);
      return;
    }
  DBG("krt_remove_route(%I/%d)\n", net->n.prefix, net->n.pxlen);
  bzero(&re, sizeof(re));
  fill_in_sockaddr((struct sockaddr_in *) &re.rt_dst, net->n.prefix, 0);
  fill_in_sockaddr((struct sockaddr_in *) &re.rt_genmask, ipa_mkmask(net->n.pxlen), 0);
  if (ioctl(if_scan_sock, SIOCDELRT, &re) < 0)
    log(L_ERR "SIOCDELRT(%I/%d): %m", net->n.prefix, net->n.pxlen);
}

void
krt_add_route(net *net, rte *new)
{
  struct rtentry re;
  rta *a = new->attrs;

  if (!krt_capable(net, new))
    {
      DBG("krt_add_route(ignored %I/%d)\n", net->n.prefix, net->n.pxlen);
      return;
    }
  DBG("krt_add_route(%I/%d)\n", net->n.prefix, net->n.pxlen);
  bzero(&re, sizeof(re));
  fill_in_sockaddr((struct sockaddr_in *) &re.rt_dst, net->n.prefix, 0);
  fill_in_sockaddr((struct sockaddr_in *) &re.rt_genmask, ipa_mkmask(net->n.pxlen), 0);
  re.rt_flags = RTF_UP;
  if (net->n.pxlen == 32)
    re.rt_flags |= RTF_HOST;
  switch (a->dest)
    {
    case RTD_ROUTER:
      fill_in_sockaddr((struct sockaddr_in *) &re.rt_gateway, a->gw, 0);
      re.rt_flags |= RTF_GATEWAY;
      break;
#ifndef CONFIG_AUTO_ROUTES
    case RTD_DEVICE:
      re.rt_dev = a->iface->name;
      break;
#endif
#ifdef RTF_REJECT
    case RTD_UNREACHABLE:
      re.rt_flags |= RTF_REJECT;
      break;
#endif
    default:
      die("krt set: unknown flags, but not filtered");
    }

  if (ioctl(if_scan_sock, SIOCADDRT, &re) < 0)
    log(L_ERR "SIOCADDRT(%I/%d): %m", net->n.prefix, net->n.pxlen);
}

void
krt_set_notify(struct proto *x, net *net, rte *new, rte *old)
{
  if (x->state != PRS_UP)
    return;
  if (old)
    krt_remove_route(net, old);
  if (new)
    krt_add_route(net, new);
}

void
krt_set_preconfig(struct krt_proto *x)
{
  if (if_scan_sock < 0)
    die("krt set: missing socket");
  x->p.rt_notify = krt_set_notify;
}
