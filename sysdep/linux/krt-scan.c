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

static int
krt_uptodate(rte *k, rte *e)
{
  rta *ka = k->attrs, *ea = e->attrs;

  if (ka->dest != ea->dest)
    return 0;
  if (ka->dest == RTD_ROUTER && !ipa_equal(ka->gw, ea->gw))
    return 0;
  /* FIXME: Device routes */
  return 1;
}

static void
krt_parse_entry(byte *ent, struct krt_proto *p)
{
  u32 dest0, gw0, mask0;
  ip_addr dest, gw, mask;
  unsigned int flags, verdict;
  int masklen;
  net *net;
  byte *iface = ent;
  rta a;
  rte *e, *old;

  if (sscanf(ent, "%*s\t%x\t%x\t%x\t%*d\t%*d\t%*d\t%x\t", &dest0, &gw0, &flags, &mask0) != 4)
    {
      log(L_ERR "krt read: unable to parse `%s'", ent);
      return;
    }
  while (*ent != '\t')
    ent++;
  *ent = 0;

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
  a.proto = &p->p;
  a.source = RTS_INHERIT;
  a.scope = SCOPE_UNIVERSE;
  a.cast = RTC_UNICAST;
  a.tos = a.flags = a.aflags = 0;
  a.from = IPA_NONE;
  a.iface = NULL;
  a.attrs = NULL;

  if (flags & RTF_GATEWAY)
    {
      neighbor *ng = neigh_find(&p->p, &gw, 0);
      if (ng)
	a.iface = ng->iface;
      else
	/* FIXME: Remove this warning? */
	log(L_WARN "Kernel told us to use non-neighbor %I for %I/%d\n", gw, net->n.prefix, net->n.pxlen);
      a.dest = RTD_ROUTER;
      a.gw = gw;
    }
  else if (flags & RTF_REJECT)
    {
      a.dest = RTD_UNREACHABLE;
      a.gw = IPA_NONE;
    }
  else
    {
      /* FIXME: Should support interface routes? */
      /* FIXME: What about systems not generating their own if routes? (see CONFIG_AUTO_ROUTES) */
      return;
    }

  e = rte_get_temp(&a);
  e->net = net;
  old = net->routes;
  if (old && !krt_capable(old))
    old = NULL;
  if (old)
    {
      if (krt_uptodate(e, net->routes))
	verdict = KRF_SEEN;
      else
	verdict = KRF_UPDATE;
    }
  else if (p->scanopt.learn && !net->routes)
    verdict = KRF_LEARN;
  else
    verdict = KRF_DELETE;

  DBG("krt_parse_entry: verdict %d\n", verdict);

  net->n.flags = verdict;
  if (verdict != KRF_SEEN)
    {
      /* Get a cached copy of attributes and link the route */
      e->attrs = rta_lookup(e->attrs);
      e->next = net->routes;
      net->routes = e;
    }
  else
    rte_free(e);
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
krt_prune(struct krt_proto *p)
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
      int verdict = f->flags;
      rte *new, *old;

      if (verdict != KRF_CREATE && verdict != KRF_SEEN)
	{
	  old = n->routes;
	  n->routes = old->next;
	}
      else
	old = NULL;
      new = n->routes;

      switch (verdict)
	{
	case KRF_CREATE:
	  if (new)
	    {
	      if (new->attrs->source == RTS_INHERIT)
		{
		  DBG("krt_prune: removing inherited %I/%d\n", n->n.prefix, n->n.pxlen);
		  rte_update(n, &p->p, NULL);
		}
	      else
		{
		  DBG("krt_prune: reinstalling %I/%d\n", n->n.prefix, n->n.pxlen);
		  krt_add_route(new);
		}
	    }
	  break;
	case KRF_SEEN:
	  /* Nothing happens */
	  break;
	case KRF_UPDATE:
	  DBG("krt_prune: updating %I/%d\n", n->n.prefix, n->n.pxlen);
	  krt_remove_route(old);
	  krt_add_route(new);
	  break;
	case KRF_DELETE:
	  DBG("krt_prune: deleting %I/%d\n", n->n.prefix, n->n.pxlen);
	  krt_remove_route(old);
	  break;
	case KRF_LEARN:
	  DBG("krt_prune: learning %I/%d\n", n->n.prefix, n->n.pxlen);
	  rte_update(n, &p->p, new);
	  break;
	default:
	  die("krt_prune: invalid route status");
	}

      if (old)
	rte_free(old);
      f->flags = 0;
    }
  FIB_WALK_END;
}

static void
krt_scan_fire(timer *t)
{
  struct krt_proto *p = t->data;

  if (krt_scan_proc(p))
    krt_prune(p);
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
