/*
 *	BIRD -- Linux Routing Table Scanning
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <net/route.h>

#define LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/route.h"
#include "nest/protocol.h"
#include "nest/iface.h"
#include "lib/timer.h"
#include "lib/unix.h"
#include "lib/krt.h"

#define SCANOPT struct krt_scan_params *p = &x->scanopt

static int krt_scan_fd = -1;

/* FIXME: Filtering */

static void
krt_magic_route(struct krt_proto *p, net *net, ip_addr gw)
{
  neighbor *ng;
  rta a, *t;
  rte *e;

  ng = neigh_find(&p->p, &gw, 0);
  if (!ng)
    {
      log(L_ERR "Kernel told us to use non-neighbor %I for %I/%d\n", gw, net->n.prefix, net->n.pxlen);
      return;
    }
  a.proto = &p->p;
  a.source = RTS_INHERIT;
  a.scope = SCOPE_UNIVERSE;
  a.cast = RTC_UNICAST;
  a.dest = RTD_ROUTER;
  a.tos = 0;
  a.flags = 0;
  a.gw = gw;
  a.from = IPA_NONE;
  a.iface = ng->iface;
  a.attrs = NULL;
  t = rta_lookup(&a);
  e = rte_get_temp(t);
  e->net = net;
  rte_update(net, &p->p, e);
}

static void
krt_parse_entry(byte *e, struct krt_proto *p)
{
  u32 dest0, gw0, mask0;
  ip_addr dest, gw, mask;
  unsigned int flags;
  int masklen;
  net *net;
  byte *iface = e;

  if (sscanf(e, "%*s\t%x\t%x\t%x\t%*d\t%*d\t%*d\t%x\t", &dest0, &gw0, &flags, &mask0) != 4)
    {
      log(L_ERR "krt read: unable to parse `%s'", e);
      return;
    }
  while (*e != '\t')
    e++;
  *e = 0;

  dest = ipa_from_u32(dest0);
  ipa_ntoh(dest);
  gw = ipa_from_u32(gw0);
  ipa_ntoh(gw);
  mask = ipa_from_u32(mask0);
  ipa_ntoh(mask);
  if ((masklen = ipa_mklen(mask)) < 0)
    {
      log(L_ERR "krt read: invalid netmask %08x", mask0);
      return;
    }
  DBG("Got %I/%d via %I flags %x\n", dest, masklen, gw, flags);

  if (!(flags & RTF_UP))
    return;
  if (flags & RTF_HOST)
    masklen = 32;
  if (flags & (RTF_DYNAMIC | RTF_MODIFIED)) /* Redirect route */
    {
      log(L_WARN "krt: Ignoring redirect to %I/%d via %I", dest, masklen, gw);
      return;
    }
  net = net_get(&master_table, 0, dest, masklen);
  if (net->routes)
    {
      rte *e = net->routes;
      rta *a = e->attrs;
      int ok;
      switch (a->dest)
	{
	case RTD_ROUTER:
	  ok = (flags & RTF_GATEWAY) && ipa_equal(gw, a->gw);
	  break;
	case RTD_DEVICE:
#ifdef CONFIG_AUTO_ROUTES
	  ok = 1;
#else
	  ok = !(flags & RTF_GATEWAY) && !strcmp(iface, a->iface->name);
#endif
	  break;
	case RTD_UNREACHABLE:
	  ok = flags & RTF_REJECT;
	default:
	  ok = 0;
	}
      net->n.flags |= ok ? KRF_SEEN : KRF_UPDATE;
    }
  else
    {
#ifdef CONFIG_AUTO_ROUTES
      if (!(flags & RTF_GATEWAY))	/* It's a device route */
	return;
#endif
      DBG("krt_parse_entry: kernel reporting unknown route %I/%d\n", dest, masklen);
      if (p->scanopt.learn)
	{
	  if (flags & RTF_GATEWAY)
	    krt_magic_route(p, net, gw);
	}
      net->n.flags |= KRF_UPDATE;
    }
}

static int
krt_scan_proc(struct krt_proto *p)
{
  byte buf[32768];
  int l, seen_hdr;

  DBG("Scanning kernel table...\n");
  if (krt_scan_fd < 0)
    {
      krt_scan_fd = open("/proc/net/route", O_RDONLY);
      if (krt_scan_fd < 0)
	die("/proc/net/route: %m");
    }
  else if (lseek(krt_scan_fd, 0, SEEK_SET) < 0)
    {
      log(L_ERR "krt seek: %m");
      return 0;
    }
  seen_hdr = 0;
  while ((l = read(krt_scan_fd, buf, sizeof(buf))) > 0)
    {
      byte *z = buf;
      if (l & 127)
	{
	  log(L_ERR "krt read: misaligned entry: l=%d", l);
	  return 0;
	}
      while (l >= 128)
	{
	  if (seen_hdr++)
	    krt_parse_entry(z, p);
	  z += 128;
	  l -= 128;
	}
    }
  if (l < 0)
    {
      log(L_ERR "krt read: %m");
      return 0;
    }
  DBG("KRT scan done, seen %d lines\n", seen_hdr);
  return 1;
}

static void
krt_prune(void)
{
  struct rtable *t = &master_table;
  struct fib_node *f;

  DBG("Pruning routes...\n");
  while (t && t->tos)
    t = t->sibling;
  if (!t)
    return;
  FIB_WALK(&t->fib, f)
    {
      net *n = (net *) f;
      switch (f->flags)
	{
	case KRF_UPDATE:
	  DBG("krt_prune: removing %I/%d\n", n->n.prefix, n->n.pxlen);
	  krt_remove_route(n, NULL);
	  /* Fall-thru */
	case 0:
	  if (n->routes)
	    {
	      DBG("krt_prune: reinstalling %I/%d\n", n->n.prefix, n->n.pxlen);
	      krt_add_route(n, n->routes);
	    }
	  break;
	case KRF_SEEN:
	  break;
	default:
	  die("krt_prune: invalid route status");
	}
      f->flags = 0;
    }
  FIB_WALK_END;
}

static void
krt_scan_fire(timer *t)
{
  struct krt_proto *p = t->data;

  if (krt_scan_proc(p))
    krt_prune();
}

void
krt_scan_preconfig(struct krt_proto *x)
{
  SCANOPT;

  p->recurrence = 60;
  p->learn = 0;
}

void
krt_scan_start(struct krt_proto *x)
{
  SCANOPT;
  timer *t = tm_new(x->p.pool);

  p->timer = t;
  t->hook = krt_scan_fire;
  t->data = x;
  t->recurrent = p->recurrence;
  krt_scan_fire(t);
  if (t->recurrent)
    tm_start(t, t->recurrent);
}

void
krt_scan_shutdown(struct krt_proto *x)
{
  SCANOPT;

  tm_stop(p->timer);
  /* FIXME: Remove all krt's? */
}
