/*
 *	BIRD -- Direct Device Routes
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include <string.h>

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/protocol.h"
#include "nest/route.h"
#include "nest/rt-dev.h"
#include "conf/conf.h"
#include "lib/resource.h"

struct proto *cf_dev_proto;

static void
dev_if_notify(struct proto *p, unsigned c, struct iface *old, struct iface *new)
{
  struct rt_dev_proto *P = (void *) p;

  if (old && !iface_patt_match(&P->iface_list, old) ||
      new && !iface_patt_match(&P->iface_list, new))
    return;
  if (c & IF_CHANGE_DOWN)
    {
      net *n;

      debug("dev_if_notify: %s going down\n", old->name);
      n = net_find(&master_table, 0, old->prefix, old->pxlen);
      if (!n)
	{
	  debug("dev_if_notify: device shutdown: prefix not found\n");
	  return;
	}
      rte_update(n, p, NULL);
    }
  else if (c & IF_CHANGE_UP)
    {
      rta *a, A;
      net *n;
      rte *e;

      debug("dev_if_notify: %s going up\n", new->name);
      bzero(&A, sizeof(A));
      A.proto = p;
      A.source = RTS_DEVICE;
      A.scope = (new->flags & IF_LOOPBACK) ? SCOPE_HOST : SCOPE_UNIVERSE;
      A.cast = RTC_UNICAST;
      A.dest = RTD_DEVICE;
      A.iface = new;
      A.attrs = NULL;
      a = rta_lookup(&A);
      if (new->flags & IF_UNNUMBERED)
	n = net_get(&master_table, 0, new->opposite, new->pxlen);
      else
	n = net_get(&master_table, 0, new->prefix, new->pxlen);
      e = rte_get_temp(a);
      e->net = n;
      e->pflags = 0;
      rte_update(n, p, e);
    }
}

static void
dev_start(struct proto *p)
{
}

static void
dev_init(struct protocol *p)
{
}

static void
dev_preconfig(struct protocol *x)
{
  struct rt_dev_proto *P = proto_new(&proto_device, sizeof(struct rt_dev_proto));
  struct proto *p = &P->p;
  struct iface_patt *k = cfg_alloc(sizeof(struct iface_patt));

  cf_dev_proto = p;
  p->preference = DEF_PREF_DIRECT;
  p->start = dev_start;
  p->if_notify = dev_if_notify;
  init_list(&P->iface_list);
  k->pattern = "*";
  add_tail(&P->iface_list, &k->n);
}

static void
dev_postconfig(struct protocol *p)
{
}

struct protocol proto_device = {
  { NULL, NULL },
  "Device",
  0,
  dev_init,
  dev_preconfig,
  dev_postconfig
};
