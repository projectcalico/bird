/*
 *	BIRD -- Neighbor Cache
 *
 *	(c) 1998--2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/protocol.h"
#include "lib/resource.h"

#define NEIGH_HASH_SIZE 256

static slab *neigh_slab;
static list sticky_neigh_list, neigh_hash_table[NEIGH_HASH_SIZE];

static inline unsigned int
neigh_hash(struct proto *p, ip_addr *a)
{
  return (p->hash_key ^ ipa_hash(*a)) & (NEIGH_HASH_SIZE-1);
}

static int
if_connected(ip_addr *a, struct iface *i) /* -1=error, 1=match, 0=no match */
{
  struct ifa *b;

  if (!(i->flags & IF_UP))
    return 0;
  WALK_LIST(b, i->addrs)
    {
      if (ipa_equal(*a, b->ip))
	return -1;
      if (b->flags & IA_UNNUMBERED)
	{
	  if (ipa_equal(*a, b->opposite))
	    return 1;
	}
      else
	{
	  if (ipa_in_net(*a, b->prefix, b->pxlen))
	    {
	      if (ipa_equal(*a, b->prefix) ||	/* Network address */
		  ipa_equal(*a, b->brd))	/* Broadcast */
		return -1;
	      return 1;
	    }
	}
      }
  return 0;
}

neighbor *
neigh_find(struct proto *p, ip_addr *a, unsigned flags)
{
  neighbor *n;
  int class;
  unsigned int h = neigh_hash(p, a);
  struct iface *i, *j;

  WALK_LIST(n, neigh_hash_table[h])	/* Search the cache */
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
	if (!j)
	  j = i;
	/* Fall-thru */
      }
  if (!j && !(flags & NEF_STICKY))
    return NULL;

  n = sl_alloc(neigh_slab);
  n->addr = *a;
  n->iface = j;
  if (j)
    {
      add_tail(&neigh_hash_table[h], &n->n);
      add_tail(&j->neighbors, &n->if_n);
    }
  else
    add_tail(&sticky_neigh_list, &n->n);
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
  int i;

  debug("Known neighbors:\n");
  WALK_LIST(n, sticky_neigh_list)
    neigh_dump(n);
  for(i=0; i<NEIGH_HASH_SIZE; i++)
    WALK_LIST(n, neigh_hash_table[i]);
  debug("\n");
}

void
neigh_if_up(struct iface *i)
{
  neighbor *n, *next;

  WALK_LIST_DELSAFE(n, next, sticky_neigh_list)
    if (!n->iface &&
	if_connected(&n->addr, i) > 0)
      {
	n->iface = i;
	add_tail(&i->neighbors, &n->if_n);
	rem_node(&n->n);
	add_tail(&neigh_hash_table[neigh_hash(n->proto, &n->addr)], &n->n);
	DBG("Waking up sticky neighbor %I\n", n->addr);
	if (n->proto->neigh_notify && n->proto->core_state != FS_FLUSHING)
	  n->proto->neigh_notify(n);
      }
}

void
neigh_if_down(struct iface *i)
{
  node *x, *y;

  WALK_LIST_DELSAFE(x, y, i->neighbors)
    {
      neighbor *n = SKIP_BACK(neighbor, if_n, x);
      DBG("Flushing neighbor %I on %s\n", n->addr, i->name);
      rem_node(&n->if_n);
      n->iface = NULL;
      if (n->proto->neigh_notify && n->proto->core_state != FS_FLUSHING)
	n->proto->neigh_notify(n);
      rem_node(&n->n);
      if (n->flags & NEF_STICKY)
	add_tail(&sticky_neigh_list, &n->n);
      else
	sl_free(neigh_slab, n);
    }
}

void
neigh_prune(void)
{
  neighbor *n;
  node *m;
  int i;

  DBG("Pruning neighbors\n");
  for(i=0; i<NEIGH_HASH_SIZE; i++)
    WALK_LIST_DELSAFE(n, m, neigh_hash_table[i])
      if (n->proto->core_state == FS_FLUSHING)
	{
	  rem_node(&n->n);
	  if (n->iface)
	    rem_node(&n->if_n);
	  sl_free(neigh_slab, n);
	}
}

void
neigh_init(pool *if_pool)
{
  int i;

  neigh_slab = sl_new(if_pool, sizeof(neighbor));
  init_list(&sticky_neigh_list);
  for(i=0; i<NEIGH_HASH_SIZE; i++)
    init_list(&neigh_hash_table[i]);
}
