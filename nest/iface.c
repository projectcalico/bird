/*
 *	BIRD -- Management of Interfaces
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "nest/bird.h"
#include "nest/iface.h"
#include "lib/resource.h"

list iface_list;

static pool *if_pool;

void
if_dump(struct iface *i)
{
  struct ifa *a;

  debug("IF%d: %s", i->index, i->name);
  if (i->flags & IF_ADMIN_DOWN)
    debug(" ADMIN-DOWN");
  if (i->flags & IF_UP)
    debug(" UP");
  if (i->flags & IF_MULTIACCESS)
    debug(" MA");
  if (i->flags & IF_UNNUMBERED)
    debug(" UNNUM");
  if (i->flags & IF_BROADCAST)
    debug(" BC");
  if (i->flags & IF_MULTICAST)
    debug(" MC");
  if (i->flags & IF_TUNNEL)
    debug(" TUNL");
  if (i->flags & IF_LOOPBACK)
    debug(" LOOP");
  if (i->flags & IF_IGNORE)
    debug(" IGN");
  debug(" MTU=%d\n", i->mtu);
  for(a=i->ifa; a; a=a->next)
    debug("\t%08x, net %08x/%-2d bc %08x -> %08x\n", _I(a->ip), _I(a->prefix), a->pxlen, _I(a->brd), _I(a->opposite));
}

void
if_dump_all(void)
{
  struct iface *i;

  debug("Known network interfaces:\n\n");
  WALK_LIST(i, iface_list)
    if_dump(i);
  debug("\n");
}

struct if_with_a {
  struct iface i;
  struct ifa a[0];
};

static struct iface *
if_copy(struct iface *j)
{
  int len;
  struct if_with_a *w;
  struct iface *i;
  struct ifa *a, **b, *c;

  len = 0;
  for(a=j->ifa; a; a=a->next)
    len++;
  w = mb_alloc(if_pool, sizeof(struct if_with_a) + len*sizeof(struct ifa));
  i = &w->i;
  c = w->a;
  memcpy(i, j, sizeof(struct iface));
  b = &i->ifa;
  a = j->ifa;
  while (a)
    {
      *b = c;
      memcpy(c, a, sizeof(struct ifa));
      b = &c->next;
      a = a->next;
      c++;
    }
  *b = NULL;
  return i;
}

static inline void
if_free(struct iface *i)
{
  mb_free(i);
}

static unsigned
if_changed(struct iface *i, struct iface *j)
{
  unsigned f = 0;
  struct ifa *x, *y;

  x = i->ifa;
  y = j->ifa;
  while (x && y)
    {
      x = x->next;
      y = y->next;
    }
  if (x || y)
    f |= IF_CHANGE_ADDR;
  if (i->mtu != j->mtu)
    f |= IF_CHANGE_MTU;
  if (i->flags != j->flags)
    {
      f |= IF_CHANGE_FLAGS;
      if ((i->flags ^ j->flags) & IF_UP)
	if (i->flags & IF_UP)
	  f |= IF_CHANGE_DOWN;
	else
	  f |= IF_CHANGE_UP;
    }
  return f;
}

static void
if_notify_change(unsigned c, struct iface *old, struct iface *new)
{
  debug("Interface change notification (%x) for %s\n", c, new->name);
}

void
if_update(struct iface *new)
{
  struct iface *i;

  WALK_LIST(i, iface_list)
    if (!strcmp(new->name, i->name))
      {
	unsigned c = if_changed(i, new);
	if (c)
	  {
	    struct iface *j = if_copy(new);
	    if_notify_change(c, i, j);
	    insert_node(&j->n, &i->n);
	    rem_node(&i->n);
	    if_free(i);
	  }
	return;
      }
  i = if_copy(new);
  add_tail(&iface_list, &i->n);
  if_notify_change(IF_CHANGE_UP | IF_CHANGE_FLAGS | IF_CHANGE_MTU | IF_CHANGE_ADDR, NULL, i);
}

void
if_end_update(void)
{
  struct iface *i, j;

  WALK_LIST(i, iface_list)
    if (i->flags & IF_UPDATED)
      i->flags &= ~IF_UPDATED;
    else
      {
	memcpy(&j, i, sizeof(struct iface));
	i->flags = (i->flags & ~IF_UP) | IF_ADMIN_DOWN;
	if_notify_change(IF_CHANGE_DOWN | IF_CHANGE_FLAGS, &j, i);
      }
}

void
if_init(void)
{
  if_pool = rp_new(&root_pool, "Interfaces");
  init_list(&iface_list);
}
