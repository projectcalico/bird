/*
 *	BIRD -- Management of Interfaces and Neighbor Cache
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/protocol.h"
#include "nest/cli.h"
#include "lib/resource.h"
#include "lib/string.h"
#include "conf/conf.h"

static pool *if_pool;

static void auto_router_id(void);

/*
 *	Neighbor Cache
 *
 *	FIXME: Use hashing to get some real speed.
 */

static slab *neigh_slab;
static list neigh_list;

static int
if_connected(ip_addr *a, struct iface *i) /* -1=error, 1=match, 0=no match */
{
  struct ifa *b;

  if (!(i->flags & IF_UP))
    return 0;
  if ((i->flags & IF_UNNUMBERED) && ipa_equal(*a, i->addr->opposite))
    return 1;
  WALK_LIST(b, i->addrs)
    if (ipa_in_net(*a, b->prefix, b->pxlen))
      {
	if (ipa_equal(*a, b->prefix) ||	/* Network address */
	    ipa_equal(*a, b->brd) ||	/* Broadcast */
	    ipa_equal(*a, b->ip))	/* Our own address */
	  return -1;
	return 1;
      }
  return 0;
}

neighbor *
neigh_find(struct proto *p, ip_addr *a, unsigned flags)
{
  neighbor *n;
  int class;
  struct iface *i, *j;

  WALK_LIST(n, neigh_list)
    if (n->proto == p && ipa_equal(*a, n->addr))
      return n;

  class = ipa_classify(*a);
  if (class < 0)			/* Invalid address */
    return NULL;
  if ((class & IADDR_SCOPE_MASK) < SCOPE_SITE ||
      !(class & IADDR_HOST))
    return NULL;			/* Bad scope or a somecast */

  j = NULL;
  WALK_LIST(i, iface_list)
    switch (if_connected(a, i))
      {
      case -1:
	return NULL;
      case 1:
	if (!j)				/* FIXME: Search for _optimal_ iface route? */
	  j = i;
	/* Fall-thru */
      }
  if (!j && !(flags & NEF_STICKY))
    return NULL;

  n = sl_alloc(neigh_slab);
  n->addr = *a;
  n->iface = j;
  add_tail(&neigh_list, &n->n);
  if (j)
    {
      n->sibling = j->neigh;
      j->neigh = n;
    }
  else
    n->sibling = NULL;
  n->proto = p;
  n->data = NULL;
  n->aux = 0;
  n->flags = flags;
  return n;
}

void
neigh_dump(neighbor *n)
{
  debug("%p %I ", n, n->addr);
  if (n->iface)
    debug("%s ", n->iface->name);
  else
    debug("[] ");
  debug("%s %p %08x", n->proto->name, n->data, n->aux);
  if (n->flags & NEF_STICKY)
    debug(" STICKY");
  debug("\n");
}

void
neigh_dump_all(void)
{
  neighbor *n;

  debug("Known neighbors:\n");
  WALK_LIST(n, neigh_list)
    neigh_dump(n);
  debug("\n");
}

static void
neigh_if_up(struct iface *i)
{
  neighbor *n;

  WALK_LIST(n, neigh_list)
    if (!n->iface &&
	if_connected(&n->addr, i) > 0)
      {
	n->iface = i;
	n->sibling = i->neigh;
	i->neigh = n;
	DBG("Waking up sticky neighbor %I\n", n->addr);
	if (n->proto->neigh_notify && n->proto->core_state != FS_FLUSHING)
	  n->proto->neigh_notify(n);
      }
}

static void
neigh_if_down(struct iface *i)
{
  neighbor *n, *m;

  for(m=i->neigh; n = m;)
    {
      m = n->sibling;
      DBG("Flushing neighbor %I on %s\n", n->addr, i->name);
      n->iface = NULL;
      if (n->proto->neigh_notify && n->proto->core_state != FS_FLUSHING)
	n->proto->neigh_notify(n);
      if (!(n->flags & NEF_STICKY))
	{
	  rem_node(&n->n);
	  sl_free(neigh_slab, n);
	}
    }
  i->neigh = NULL;
}

void
neigh_prune(void)
{
  neighbor *n, *m, **N;
  struct iface *i;

  DBG("Pruning neighbors\n");
  WALK_LIST(i, iface_list)
    {
      N = &i->neigh;
      while (n = *N)
	{
	  if (n->proto->core_state == FS_FLUSHING)
	    {
	      *N = n->sibling;
	      rem_node(&n->n);
	      sl_free(neigh_slab, n);
	      continue;
	    }
	  N = &n->sibling;
	}
    }
}

/*
 *	The Interface List
 */

list iface_list;

void
ifa_dump(struct ifa *a)
{
  debug("\t%I, net %I/%-2d bc %I -> %I%s%s\n", a->ip, a->prefix, a->pxlen, a->brd, a->opposite,
	(a->flags & IF_UP) ? "" : " DOWN",
	(a->flags & IA_PRIMARY) ? "" : " SEC");
}

void
if_dump(struct iface *i)
{
  struct ifa *a;

  debug("IF%d: %s", i->index, i->name);
  if (i->flags & IF_ADMIN_DOWN)
    debug(" ADMIN-DOWN");
  if (i->flags & IF_UP)
    debug(" UP");
  else
    debug(" DOWN");
  if (i->flags & IF_LINK_UP)
    debug(" LINK-UP");
  if (i->flags & IF_MULTIACCESS)
    debug(" MA");
  if (i->flags & IF_UNNUMBERED)
    debug(" UNNUM");
  if (i->flags & IF_BROADCAST)
    debug(" BC");
  if (i->flags & IF_MULTICAST)
    debug(" MC");
  if (i->flags & IF_LOOPBACK)
    debug(" LOOP");
  if (i->flags & IF_IGNORE)
    debug(" IGN");
  if (i->flags & IF_TMP_DOWN)
    debug(" TDOWN");
  debug(" MTU=%d\n", i->mtu);
  WALK_LIST(a, i->addrs)
    {
      ifa_dump(a);
      ASSERT((a != i->addr) == !(a->flags & IA_PRIMARY));
    }
}

void
if_dump_all(void)
{
  struct iface *i;

  debug("Known network interfaces:\n");
  WALK_LIST(i, iface_list)
    if_dump(i);
  debug("Router ID: %08x\n", config->router_id);
}

static inline unsigned
if_what_changed(struct iface *i, struct iface *j)
{
  unsigned c;

  if (((i->flags ^ j->flags) & ~(IF_UP | IF_ADMIN_DOWN | IF_UPDATED | IF_LINK_UP | IF_TMP_DOWN | IF_JUST_CREATED))
      || i->index != j->index)
    return IF_CHANGE_TOO_MUCH;
  c = 0;
  if ((i->flags ^ j->flags) & IF_UP)
    c |= (i->flags & IF_UP) ? IF_CHANGE_DOWN : IF_CHANGE_UP;
  if (i->mtu != j->mtu)
    c |= IF_CHANGE_MTU;
  return c;
}

static inline void
if_copy(struct iface *to, struct iface *from)
{
  to->flags = from->flags | (to->flags & IF_TMP_DOWN);
  to->mtu = from->mtu;
}

static void
ifa_notify_change(unsigned c, struct ifa *a)
{
  struct proto *p;

  debug("IFA change notification (%x) for %s:%I\n", c, a->iface->name, a->ip);
  WALK_LIST(p, active_proto_list)
    if (p->ifa_notify)
      p->ifa_notify(p, c, a);
}

static void
if_notify_change(unsigned c, struct iface *i)
{
  struct proto *p;
  struct ifa *a;

  if (i->flags & IF_JUST_CREATED)
    {
      i->flags &= ~IF_JUST_CREATED;
      c |= IF_CHANGE_CREATE | IF_CHANGE_MTU;
    }

  debug("Interface change notification (%x) for %s\n", c, i->name);
  if_dump(i);

  if (c & IF_CHANGE_UP)
    neigh_if_up(i);
  if (c & IF_CHANGE_DOWN)
    WALK_LIST(a, i->addrs)
      {
	a->flags = (i->flags & ~IA_FLAGS) | (a->flags & IA_FLAGS);
	ifa_notify_change(IF_CHANGE_DOWN, a);
      }

  WALK_LIST(p, active_proto_list)
    if (p->if_notify)
      p->if_notify(p, c, i);

  if (c & IF_CHANGE_UP)
    WALK_LIST(a, i->addrs)
      {
	a->flags = (i->flags & ~IA_FLAGS) | (a->flags & IA_FLAGS);
	ifa_notify_change(IF_CHANGE_UP, a);
      }
  if (c & IF_CHANGE_DOWN)
    neigh_if_down(i);
}

static unsigned
if_recalc_flags(struct iface *i, unsigned flags)
{
  if ((flags & (IF_ADMIN_DOWN | IF_TMP_DOWN)) ||
      !(flags & IF_LINK_UP) ||
      !i->addr)
    flags &= ~IF_UP;
  else
    flags |= IF_UP;
  return flags;
}

static void
if_change_flags(struct iface *i, unsigned flags)
{
  unsigned of = i->flags;

  i->flags = if_recalc_flags(i, flags);
  if ((i->flags ^ of) & IF_UP)
    if_notify_change((i->flags & IF_UP) ? IF_CHANGE_UP : IF_CHANGE_DOWN, i);
}

struct iface *
if_update(struct iface *new)
{
  struct iface *i;
  struct ifa *a, *b;
  unsigned c;

  WALK_LIST(i, iface_list)
    if (!strcmp(new->name, i->name))
      {
	new->addr = i->addr;
	new->flags = if_recalc_flags(new, new->flags);
	c = if_what_changed(i, new);
	if (c & IF_CHANGE_TOO_MUCH)	/* Changed a lot, convert it to down/up */
	  {
	    DBG("Interface %s changed too much -- forcing down/up transition\n", i->name);
	    if_change_flags(i, i->flags | IF_TMP_DOWN);
	    rem_node(&i->n);
	    new->addr = i->addr;
	    memcpy(&new->addrs, &i->addrs, sizeof(i->addrs));
	    memcpy(i, new, sizeof(*i));
	    goto newif;
	  }
	else if (c)
	  {
	    if_copy(i, new);
	    if_notify_change(c, i);
	  }
	i->flags |= IF_UPDATED;
	return i;
      }
  i = mb_alloc(if_pool, sizeof(struct iface));
  memcpy(i, new, sizeof(*i));
  init_list(&i->addrs);
newif:
  i->flags |= IF_UPDATED | IF_TMP_DOWN;		/* Tmp down as we don't have addresses yet */
  add_tail(&iface_list, &i->n);
  return i;
}

void
if_start_update(void)
{
  struct iface *i;
  struct ifa *a;

  WALK_LIST(i, iface_list)
    {
      i->flags &= ~IF_UPDATED;
      WALK_LIST(a, i->addrs)
	a->flags &= ~IF_UPDATED;
    }
}

void
if_end_partial_update(struct iface *i)
{
  if (i->flags & IF_TMP_DOWN)
    if_change_flags(i, i->flags & ~IF_TMP_DOWN);
}

void
if_end_update(void)
{
  struct iface *i, j;
  struct ifa *a, *b;

  if (!config->router_id)
    auto_router_id();

  WALK_LIST(i, iface_list)
    {
      if (!(i->flags & IF_UPDATED))
	if_change_flags(i, (i->flags & ~IF_LINK_UP) | IF_ADMIN_DOWN);
      else
	{
	  WALK_LIST_DELSAFE(a, b, i->addrs)
	    if (!(a->flags & IF_UPDATED))
	      ifa_delete(a);
	  if_end_partial_update(i);
	}
    }
}

void
if_feed_baby(struct proto *p)
{
  struct iface *i;
  struct ifa *a;

  if (!p->if_notify && !p->ifa_notify)
    return;
  debug("Announcing interfaces to new protocol %s\n", p->name);
  WALK_LIST(i, iface_list)
    {
      if (p->if_notify)
	p->if_notify(p, IF_CHANGE_CREATE | ((i->flags & IF_UP) ? IF_CHANGE_UP : 0), i);
      if (p->ifa_notify && (i->flags & IF_UP))
	WALK_LIST(a, i->addrs)
	  p->ifa_notify(p, IF_CHANGE_CREATE | IF_CHANGE_UP, a);
    }
}

struct iface *
if_find_by_index(unsigned idx)
{
  struct iface *i;

  WALK_LIST(i, iface_list)
    if (i->index == idx)
      return i;
  return NULL;
}

struct iface *
if_find_by_name(char *name)
{
  struct iface *i;

  WALK_LIST(i, iface_list)
    if (!strcmp(i->name, name))
      return i;
  return NULL;
}

static int
ifa_recalc_primary(struct iface *i)
{
  struct ifa *a, *b = NULL;
  int res;

  WALK_LIST(a, i->addrs)
    {
      if (!(a->flags & IA_SECONDARY) && (!b || a->scope > b->scope))
	b = a;
      a->flags &= ~IA_PRIMARY;
    }
  res = (b != i->addr);
  i->addr = b;
  if (b)
    {
      b->flags |= IA_PRIMARY;
      rem_node(&b->n);
      add_head(&i->addrs, &b->n);
    }
  return res;
}

struct ifa *
ifa_update(struct ifa *a)
{
  struct iface *i = a->iface;
  struct ifa *b;

  WALK_LIST(b, i->addrs)
    if (ipa_equal(b->ip, a->ip))
      {
	if (ipa_equal(b->prefix, a->prefix) &&
	    b->pxlen == a->pxlen &&
	    ipa_equal(b->brd, a->brd) &&
	    ipa_equal(b->opposite, a->opposite) &&
	    b->scope == a->scope)
	  {
	    b->flags |= IF_UPDATED;
	    return b;
	  }
	ifa_delete(b);
	break;
      }
  b = mb_alloc(if_pool, sizeof(struct ifa));
  memcpy(b, a, sizeof(struct ifa));
  add_tail(&i->addrs, &b->n);
  b->flags = (i->flags & ~IA_FLAGS) | (a->flags & IA_FLAGS);
  if ((!i->addr || i->addr->scope < b->scope) && ifa_recalc_primary(i))
    if_change_flags(i, i->flags | IF_TMP_DOWN);
  if (b->flags & IF_UP)
    ifa_notify_change(IF_CHANGE_CREATE | IF_CHANGE_UP, b);
  return b;
}

void
ifa_delete(struct ifa *a)
{
  struct iface *i = a->iface;
  struct ifa *b;

  WALK_LIST(b, i->addrs)
    if (ipa_equal(b->ip, a->ip))
      {
	rem_node(&b->n);
	if (b->flags & IF_UP)
	  {
	    b->flags &= ~IF_UP;
	    ifa_notify_change(IF_CHANGE_DOWN, b);
	  }
	if (b->flags & IA_PRIMARY)
	  {
	    if_change_flags(i, i->flags | IF_TMP_DOWN);
	    ifa_recalc_primary(i);
	  }
	mb_free(b);
	return;
      }
}

static void
auto_router_id(void)
{
#ifndef IPV6
  struct iface *i, *j;

  j = NULL;
  WALK_LIST(i, iface_list)
    if ((i->flags & IF_LINK_UP) &&
	!(i->flags & (IF_UNNUMBERED | IF_IGNORE | IF_ADMIN_DOWN)) &&
	i->addr &&
	(!j || ipa_to_u32(i->addr->ip) < ipa_to_u32(j->addr->ip)))
      j = i;
  if (!j)
    die("Cannot determine router ID (no suitable network interface found), please configure it manually");
  debug("Guessed router ID %I (%s)\n", j->addr->ip, j->name);
  config->router_id = ipa_to_u32(j->addr->ip);
#endif
}

void
if_init(void)
{
  if_pool = rp_new(&root_pool, "Interfaces");
  init_list(&iface_list);
  neigh_slab = sl_new(if_pool, sizeof(neighbor));
  init_list(&neigh_list);
}

/*
 *	Interface Pattern Lists
 */

struct iface_patt *
iface_patt_match(list *l, struct iface *i)
{
  struct iface_patt *p;

  WALK_LIST(p, *l)
    {
      char *t = p->pattern;
      int ok = 1;
      if (t)
	{
	  if (*t == '-')
	    {
	      t++;
	      ok = 0;
	    }
	  if (!patmatch(t, i->name))
	    continue;
	}
      if (!i->addr || !ipa_in_net(i->addr->ip, p->prefix, p->pxlen))
	continue;
      return ok ? p : NULL;
    }
  return NULL;
}

int
iface_patts_equal(list *a, list *b, int (*comp)(struct iface_patt *, struct iface_patt *))
{
  struct iface_patt *x, *y;

  x = HEAD(*a);
  y = HEAD(*b);
  while (x->n.next && y->n.next)
    {
      if (strcmp(x->pattern, y->pattern) ||
	  !ipa_equal(x->prefix, y->prefix) ||
	  x->pxlen != y->pxlen ||
	  comp && !comp(x, y))
	return 0;
      x = (void *) x->n.next;
      y = (void *) y->n.next;
    }
  return (!x->n.next && !y->n.next);
}

/*
 *  CLI commands.
 */

static void
if_show_addr(struct ifa *a)
{
  byte broad[STD_ADDRESS_P_LENGTH + 16];
  byte opp[STD_ADDRESS_P_LENGTH + 16];

  if (ipa_nonzero(a->brd))
    bsprintf(broad, ", broadcast %I", a->brd);
  else
    broad[0] = 0;
  if (ipa_nonzero(a->opposite))
    bsprintf(opp, ", opposite %I", a->opposite);
  else
    opp[0] = 0;
  cli_msg(-1003, "\t%I/%d (%s%s%s, scope %s)",
	  a->ip, a->pxlen,
	  (a->flags & IA_PRIMARY) ? "Primary" : (a->flags & IA_SECONDARY) ? "Secondary" : "???",
	  broad, opp,
	  ip_scope_text(a->scope));
}

void
if_show(void)
{
  struct iface *i;
  struct ifa *a;
  char *type;

  WALK_LIST(i, iface_list)
    {
      cli_msg(-1001, "%s %s (index=%d)", i->name, (i->flags & IF_UP) ? "up" : "DOWN", i->index);
      if (i->flags & IF_UNNUMBERED)
	type = "UnNum-PtP";
      else if (!(i->flags & IF_MULTIACCESS))
	type = "PtP";
      else
	type = "MultiAccess";
      cli_msg(-1004, "\t%s%s%s Admin%s Link%s%s%s MTU=%d",
	      type,
	      (i->flags & IF_BROADCAST) ? " Broadcast" : "",
	      (i->flags & IF_MULTICAST) ? " Multicast" : "",
	      (i->flags & IF_ADMIN_DOWN) ? "Down" : "Up",
	      (i->flags & IF_LINK_UP) ? "Up" : "Down",
	      (i->flags & IF_LOOPBACK) ? " Loopback" : "",
	      (i->flags & IF_IGNORE) ? " Ignored" : "",
	      i->mtu);
      if (i->addr)
	if_show_addr(i->addr);
      WALK_LIST(a, i->addrs)
	if (a != i->addr)
	  if_show_addr(a);
    }
  cli_msg(0, "");
}

void
if_show_summary(void)
{
  struct iface *i;
  byte addr[STD_ADDRESS_P_LENGTH + 16];

  cli_msg(-2005, "interface state address");
  WALK_LIST(i, iface_list)
    {
      if (i->addr)
	bsprintf(addr, "%I/%d", i->addr->ip, i->addr->pxlen);
      else
	addr[0] = 0;
      cli_msg(-1005, "%-9s %-5s %s", i->name, (i->flags & IF_UP) ? "up" : "DOWN", addr);
    }
  cli_msg(0, "");
}
