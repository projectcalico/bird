/*
 *	BIRD -- BGP Attributes
 *
 *	(c) 2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/protocol.h"
#include "nest/route.h"
#include "conf/conf.h"
#include "lib/resource.h"
#include "lib/string.h"
#include "lib/unaligned.h"

#include "bgp.h"

static int bgp_check_origin(byte *a, int len)
{
  if (len > 2)
    return 6;
  return 0;
}

static int bgp_check_path(byte *a, int len)
{
  while (len)
    {
      DBG("Path segment %02x %02x\n", a[0], a[1]);
      if (len < 2 ||
	  a[0] != BGP_PATH_AS_SET && a[0] != BGP_PATH_AS_SEQUENCE ||
	  2*a[1] + 2 > len)
	return 11;
      len -= 2*a[1] + 2;
      a += 2*a[1] + 2;
    }
  return 0;
}

static int bgp_check_next_hop(byte *a, int len)
{
  ip_addr addr;

  memcpy(&addr, a, len);
  if (ipa_classify(ipa_ntoh(addr)) & IADDR_HOST)
    return 0;
  else
    return 8;
}

struct attr_desc {
  int expected_length;
  int expected_flags;
  int type;
  int (*validate)(byte *attr, int len);
};

static struct attr_desc bgp_attr_table[] = {
  { -1, 0, 0, NULL },							/* Undefined */
  { 1, BAF_TRANSITIVE, EAF_TYPE_INT, bgp_check_origin },		/* BA_ORIGIN */
  { -1, BAF_TRANSITIVE, EAF_TYPE_AS_PATH, bgp_check_path },		/* BA_AS_PATH */
  { 4, BAF_TRANSITIVE, EAF_TYPE_IP_ADDRESS, bgp_check_next_hop },	/* BA_NEXT_HOP */
  { 4, BAF_OPTIONAL, EAF_TYPE_INT, NULL },				/* BA_MULTI_EXIT_DISC */
  { 4, BAF_OPTIONAL, EAF_TYPE_INT, NULL },				/* BA_LOCAL_PREF */
  { 0, BAF_OPTIONAL, EAF_TYPE_OPAQUE, NULL },				/* BA_ATOMIC_AGGR */
  { 6, BAF_OPTIONAL, EAF_TYPE_OPAQUE, NULL },				/* BA_AGGREGATOR */
#if 0
  /* FIXME: Handle community lists */
  { 0, 0 },								/* BA_COMMUNITY */
  { 0, 0 },								/* BA_ORIGINATOR_ID */
  { 0, 0 },								/* BA_CLUSTER_LIST */
#endif
};

static int bgp_mandatory_attrs[] = { BA_ORIGIN, BA_AS_PATH, BA_NEXT_HOP };

struct rta *
bgp_decode_attrs(struct bgp_conn *conn, byte *attr, unsigned int len, struct linpool *pool)
{
  struct bgp_proto *bgp = conn->bgp;
  rta *a = lp_alloc(pool, sizeof(struct rta));
  unsigned int flags, code, l, errcode, i, type;
  byte *z, *attr_start;
  byte seen[256/8];
  eattr *e;
  ea_list *ea;
  struct adata *ad;
  neighbor *neigh;
  ip_addr nexthop;

  a->proto = &bgp->p;
  a->source = RTS_BGP;
  a->scope = SCOPE_UNIVERSE;
  a->cast = RTC_UNICAST;
  a->dest = RTD_ROUTER;
  a->flags = 0;
  a->aflags = 0;
  a->from = bgp->cf->remote_ip;
  a->eattrs = NULL;

  /* Parse the attributes */
  bzero(seen, sizeof(seen));
  DBG("BGP: Parsing attributes\n");
  while (len)
    {
      if (len < 2)
	goto malformed;
      attr_start = attr;
      flags = *attr++;
      code = *attr++;
      len -= 2;
      if (flags & BAF_EXT_LEN)
	{
	  if (len < 2)
	    goto malformed;
	  l = get_u16(attr);
	  attr += 2;
	  len -= 2;
	}
      else
	{
	  if (len < 1)
	    goto malformed;
	  l = *attr++;
	  len--;
	}
      if (l > len)
	goto malformed;
      len -= l;
      z = attr;
      attr += l;
      DBG("Attr %02x %02x %d\n", code, flags, l);
      if (seen[code/8] & (1 << (code%8)))
	goto malformed;
      seen[code/8] |= (1 << (code%8));
      if (code && code < sizeof(bgp_attr_table)/sizeof(bgp_attr_table[0]))
	{
	  struct attr_desc *desc = &bgp_attr_table[code];
	  if (desc->expected_length >= 0 && desc->expected_length != (int) l)
	    { errcode = 5; goto err; }
	  if ((desc->expected_flags ^ flags) & (BAF_OPTIONAL | BAF_TRANSITIVE))
	    { errcode = 4; goto err; }
	  if (desc->validate && (errcode = desc->validate(z, l)))
	    goto err;
	  type = desc->type;
	}
      else				/* Unknown attribute */
	{				/* FIXME: Send partial bit when forwarding */
	  if (!(flags & BAF_OPTIONAL))
	    { errcode = 2; goto err; }
	  type = EAF_TYPE_OPAQUE;
	}
      ea = lp_alloc(pool, sizeof(struct ea_list) + sizeof(struct eattr));
      ea->next = a->eattrs;
      a->eattrs = ea;
      ea->flags = 0;
      ea->count = 1;
      ea->attrs[0].id = EA_CODE(EAP_BGP, code);
      ea->attrs[0].flags = flags;
      ea->attrs[0].type = type;
      if (type & EAF_EMBEDDED)
	ad = NULL;
      else
	{
	  ad = lp_alloc(pool, sizeof(struct adata) + l);
	  ea->attrs[0].u.ptr = ad;
	  ad->length = l;
	  memcpy(ad->data, z, l);
	}
      switch (type)
	{
	case EAF_TYPE_ROUTER_ID:
	case EAF_TYPE_INT:
	  ea->attrs[0].u.data = get_u32(z);
	  break;
	case EAF_TYPE_IP_ADDRESS:
	  *(ip_addr *)ad->data = ipa_ntoh(*(ip_addr *)ad->data);
	  break;
	}
    }

  /* Check if all mandatory attributes are present */
  for(i=0; i < sizeof(bgp_mandatory_attrs)/sizeof(bgp_mandatory_attrs[0]); i++)
    {
      code = bgp_mandatory_attrs[i];
      if (!(seen[code/8] & (1 << (code%8))))
	{
	  bgp_error(conn, 3, 3, code, 1);
	  return NULL;
	}
    }

  /* Fill in the remaining rta fields */
  e = ea_find(a->eattrs, EA_CODE(EAP_BGP, BA_NEXT_HOP));
  ASSERT(e);
  nexthop = *(ip_addr *) e->u.ptr->data;
  neigh = neigh_find(&bgp->p, &nexthop, 0);
  if (!neigh)
    {
      if (bgp->cf->multihop)
	neigh = neigh_find(&bgp->p, &bgp->cf->multihop_via, 0);
      else
	neigh = neigh_find(&bgp->p, &bgp->cf->remote_ip, 0);
    }
  if (!neigh || !neigh->iface)
    {
      DBG("BGP: No route to peer!\n");	/* FIXME */
      return NULL;
    }
  a->gw = neigh->addr;
  a->iface = neigh->iface;
  return rta_lookup(a);

malformed:
  bgp_error(conn, 3, 1, len, 0);
  return NULL;

err:
  bgp_error(conn, 3, errcode, code, 0);	/* FIXME: Return attribute data! */
  return NULL;
}
