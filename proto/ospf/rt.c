/*
 * BIRD -- OSPF
 * 
 * (c) 2000--2004 Ondrej Filip <feela@network.cz>
 * 
 * Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"
static void
add_cand(list * l, struct top_hash_entry *en,
	 struct top_hash_entry *par, u16 dist, struct ospf_area *oa);
static void
calc_next_hop(struct top_hash_entry *en,
	      struct top_hash_entry *par, struct ospf_area *oa);
static void ospf_ext_spfa(struct ospf_area *oa);
static void rt_sync(struct proto_ospf *po);

static void
fill_ri(orta * orta)
{
  orta->type = RTS_DUMMY;
  orta->capa = 0;
#define ORTA_ASBR 1
#define ORTA_ABR 2
  orta->oa = NULL;
  orta->metric1 = LSINFINITY;
  orta->metric2 = LSINFINITY;
  orta->nh = ipa_from_u32(0);
  orta->ifa = NULL;
  orta->ar = NULL;
  orta->tag = 0;
}

void
ospf_rt_initort(struct fib_node *fn)
{
  ort *ri = (ort *) fn;
  fill_ri(&ri->n);
  ri->dest = ORT_UNDEF;
  memcpy(&ri->o, &ri->n, sizeof(orta));
}

/* If new is better return 1 */
static int
ri_better(struct proto_ospf *po, orta * new, orta * old)
{
  int newtype = new->type;
  int oldtype = old->type;

  if (old->type == RTS_DUMMY)
    return 1;

  if (old->metric1 == LSINFINITY)
    return 1;

  if (po->rfc1583)
  {
    if ((newtype == RTS_OSPF) && (new->oa->areaid == 0)) newtype = RTS_OSPF_IA;
    if ((oldtype == RTS_OSPF) && (old->oa->areaid == 0)) oldtype = RTS_OSPF_IA;
  }
  
  if (new->type < old->type)
    return 1;

  if (new->metric2 < old->metric2)
  {
    if (old->metric2 == LSINFINITY)
      return 0;			/* Old is E1, new is E2 */

    return 1;			/* Both are E2 */
  }
  if (new->metric2 > old->metric2)
  {
    if (new->metric2 == LSINFINITY)
      return 1;			/* New is E1, old is E2 */

    return 0;			/* Both are E2 */
  }

  /*
   * E2 metrics are the same. It means that: 1) Paths are E2 with same
   * metric 2) Paths are E1.
   */
  if (new->metric1 < old->metric1)
    return 1;

  if (new->metric1 > old->metric1)
    return 0;

  /* Metric 1 are the same */
  if (new->oa->areaid > old->oa->areaid) return 1;	/* Larger AREAID is preffered */

  return 0;			/* Old is shorter or same */
}

static void
ri_install(struct proto_ospf *po, ip_addr prefix, int pxlen, int dest,
	   orta * new)
{
  ort *old = (ort *) fib_get(&po->rtf[dest], &prefix, pxlen);

  if (ri_better(po, new, &old->n))
  {
    memcpy(&old->n, new, sizeof(orta));
  }
}

/**
 * ospf_rt_spf - calculate internal routes
 * @po: OSPF protocol
 *
 * Calculation of internal paths in an area is described in 16.1 of RFC 2328.
 * It's based on Dijkstra's shortest path tree algorithms.
 * RFC recommends to add ASBR routers into routing table. I don't do this
 * and latter parts of routing table calculation look directly into LSA
 * Database. This function is invoked from ospf_disp().
 */
static void
ospf_rt_spfa(struct ospf_area *oa)
{
  u32 i, *rts;
  struct ospf_lsa_rt *rt;
  struct ospf_lsa_rt_link *rtl, *rr;
  struct proto *p = &oa->po->proto;
  struct proto_ospf *po = oa->po;
  struct ospf_lsa_net *ln;
  orta nf;

  if (oa->rt == NULL)
    return;

  OSPF_TRACE(D_EVENTS, "Starting routing table calculation for area %I",
	     oa->areaid);

  if (oa->rt->dist != LSINFINITY)
    ospf_age(oa);

  init_list(&oa->cand);		/* Empty list of candidates */
  oa->trcap = 0;

  DBG("LSA db prepared, adding me into candidate list.\n");

  oa->rt->dist = 0;
  oa->rt->color = CANDIDATE;
  add_head(&oa->cand, &oa->rt->cn);
  DBG("RT LSA: rt: %I, id: %I, type: %u\n", oa->rt->lsa.rt,
      oa->rt->lsa.id, oa->rt->lsa.type);

  while (!EMPTY_LIST(oa->cand))
  {
    struct top_hash_entry *act, *tmp;
    node *n;

    n = HEAD(oa->cand);
    act = SKIP_BACK(struct top_hash_entry, cn, n);
    rem_node(n);

    DBG("Working on LSA: rt: %I, id: %I, type: %u\n", act->lsa.rt,
	act->lsa.id, act->lsa.type);

    act->color = INSPF;
    switch (act->lsa.type)
    {
    case LSA_T_RT:
      rt = (struct ospf_lsa_rt *) act->lsa_body;
      if (rt->veb.bit.v)
	oa->trcap = 1;
      if (rt->veb.bit.b || rt->veb.bit.e)
      {
	nf.type = RTS_OSPF;
	nf.capa = 0;
        if (rt->veb.bit.b) nf.capa |= ORTA_ABR;
        if (rt->veb.bit.e) nf.capa |= ORTA_ASBR;
	nf.metric1 = act->dist;
	nf.metric2 = LSINFINITY;
	nf.oa = oa;
	nf.ar = act;
	nf.nh = act->nh;
	nf.ifa = act->nhi;
	ri_install(po, ipa_from_u32(act->lsa.id), 32, ORT_ROUTER, &nf);
      }
      rr = (struct ospf_lsa_rt_link *) (rt + 1);
      DBG("  Number of links: %u\n", rt->links);
      for (i = 0; i < rt->links; i++)
      {
	tmp = NULL;
	rtl = (rr + i);
	DBG("     Working on link: %I (type: %u)  ", rtl->id, rtl->type);
	switch (rtl->type)
	{
	case LSART_STUB:
	  /*
	   * This violates rfc2328! but I hope
	   * it's also correct.
	   */
	  DBG("\n");
	  if (act == oa->rt)
	    continue;
	  if (!act->nhi)
	    continue;
	  nf.type = RTS_OSPF;
	  nf.capa = 0;
	  nf.metric1 = act->dist + rtl->metric;
	  nf.metric2 = LSINFINITY;
	  nf.oa = oa;
	  nf.ar = act;
	  nf.nh = act->nh;
	  nf.ifa = act->nhi;
	  ri_install(po, ipa_from_u32(rtl->id),
		     ipa_mklen(ipa_from_u32(rtl->data)), ORT_NET, &nf);
	  break;

	case LSART_VLNK:	/* FIXME !!!!!!!! */
	  DBG("Ignoring\n");
	  continue;
	  break;
	case LSART_NET:
	  tmp = ospf_hash_find(oa->gr, rtl->id, rtl->id, LSA_T_NET);
	  if (tmp == NULL)
	    DBG("Not found!\n");
	  else
	    DBG("Found. :-)\n");
	  break;
	case LSART_PTP:
	  tmp = ospf_hash_find(oa->gr, rtl->id, rtl->id, LSA_T_RT);
	  DBG("PTP found.\n");
	  break;
	default:
	  log("Unknown link type in router lsa.");
	  break;
	}
	if (tmp)
	  DBG("Going to add cand, Mydist: %u, Req: %u\n",
	      tmp->dist, act->dist + rtl->metric);
	add_cand(&oa->cand, tmp, act, act->dist + rtl->metric, oa);
      }
      break;
    case LSA_T_NET:
      ln = act->lsa_body;
      nf.type = RTS_OSPF;
      nf.capa = 0;
      nf.metric1 = act->dist;
      nf.metric2 = LSINFINITY;
      nf.oa = oa;
      nf.ar = act;
      nf.nh = act->nh;
      nf.ifa = act->nhi;
      ri_install(po, ipa_and(ipa_from_u32(act->lsa.id), ln->netmask),
		 ipa_mklen(ln->netmask), ORT_NET, &nf);

      rts = (u32 *) (ln + 1);
      for (i = 0; i < (act->lsa.length - sizeof(struct ospf_lsa_header) -
		       sizeof(struct ospf_lsa_net)) / sizeof(u32); i++)
      {
	DBG("     Working on router %I ", *(rts + i));
	tmp = ospf_hash_find(oa->gr, *(rts + i), *(rts + i), LSA_T_RT);
	if (tmp != NULL)
	  DBG("Found :-)\n");
	else
	  DBG("Not found!\n");
	add_cand(&oa->cand, tmp, act, act->dist, oa);
      }
      break;
    }
  }
}


void
ospf_rt_spf(struct proto_ospf *po)
{
  struct proto *p = &po->proto;
  struct ospf_area *oa;
  int i;
  ort *ri;

  OSPF_TRACE(D_EVENTS, "Starting routing table calculation");

  /* Invalidate old routing table */
  for (i = 0; i < 2; i++)
    FIB_WALK(&po->rtf[i], nftmp)
  {
    ri = (ort *) nftmp;
    memcpy(&ri->o, &ri->n, sizeof(orta));	/* Backup old data */
    fill_ri(&ri->n);
  }
  FIB_WALK_END;


  WALK_LIST(oa, po->area_list)
  {
    ospf_rt_spfa(oa);
  }

  if (po->areano > 1)
  {
    //ospf_rt_sum(oa);
  }
  else
  {
    WALK_LIST(oa, po->area_list)
    {
      //if (oa->id == 0) ospf_rt_sum(oa);
    }

    WALK_LIST(oa, po->area_list)
    {
      //if (oa->trcap == 1) ospf_rt_sum(oa);
    }
  }
  WALK_LIST(oa, po->area_list)
  {
    if (!oa->stub)
    {
      ospf_ext_spfa(oa);
      break;
    }
  }
  rt_sync(po);
}


/**
 * ospf_ext_spfa - calculate external paths
 * @po: protocol
 *
 * After routing table for any area is calculated, calculation of external
 * path is invoked. This process is described in 16.6 of RFC 2328.
 * Inter- and Intra-area paths are always prefered over externals.
 */
static void
ospf_ext_spfa(struct ospf_area *oa)
{
  struct proto_ospf *po = oa->po;
  ort *nf1, *nf2;
  orta nfa;
  struct top_hash_entry *en;
  struct proto *p = &po->proto;
  struct ospf_lsa_ext *le;
  struct ospf_lsa_ext_tos *lt;
  int mlen;
  ip_addr ip, nh, rtid;
  struct iface *nhi = NULL;
  int met1, met2;
  neighbor *nn;
  struct ospf_lsa_rt *rt;


  OSPF_TRACE(D_EVENTS, "Starting routing table calculation for ext routes");

  WALK_SLIST(en, oa->lsal)
  {
    if (en->lsa.type != LSA_T_EXT)
      continue;
    if (en->lsa.age == LSA_MAXAGE)
      continue;
    if (en->lsa.rt == p->cf->global->router_id)
      continue;

    le = en->lsa_body;
    lt = (struct ospf_lsa_ext_tos *) (le + 1);

    DBG("%s: Working on LSA. ID: %I, RT: %I, Type: %u, Mask %I\n",
	p->name, en->lsa.id, en->lsa.rt, en->lsa.type, le->netmask);

    if (lt->metric == LSINFINITY)
      continue;
    ip = ipa_and(ipa_from_u32(en->lsa.id), le->netmask);
    mlen = ipa_mklen(le->netmask);
    if ((mlen < 0) || (mlen > 32))
    {
      log("%s: Invalid mask in LSA. ID: %I, RT: %I, Type: %u, Mask %I",
	  p->name, en->lsa.id, en->lsa.rt, en->lsa.type, le->netmask);
      continue;
    }
    nhi = NULL;
    nh = IPA_NONE;

    met1 = LSINFINITY;
    met2 = LSINFINITY;

    rtid = ipa_from_u32(en->lsa.rt);

    if (!(nf1 = fib_find(&po->rtf[ORT_ROUTER], &rtid, 32)))
      continue;			/* No AS boundary router found */

    if (nf1->n.metric1 == LSINFINITY)
      continue;			/* distance is INF */

    if (!(nf1->n.capa & ORTA_ASBR))
      continue;			/* It is not ASBR */

    if (ipa_compare(lt->fwaddr, ipa_from_u32(0)) == 0)
    {
      if (lt->etos > 0)
      {				/* FW address == 0 */
	met1 = nf1->n.metric1;
	met2 = lt->metric;
      }
      else
      {
	met1 = nf1->n.metric1 + lt->metric;
	met2 = LSINFINITY;
      }
      nh = nf1->n.nh;
      nhi = nf1->n.ifa;
    }
    else
    {				/* FW address !=0 */
      if (!(nf2 = fib_route(&po->rtf[ORT_NET], lt->fwaddr, 32)))
      {
	DBG("Cannot find network route (GW=%I)\n", lt->fwaddr);
	continue;
      }
      if (lt->etos > 0)
      {
	met1 = nf2->n.metric1;
	met2 = lt->metric;
      }
      else
      {
	met1 = nf2->n.metric1 + lt->metric;
	met2 = LSINFINITY;
      }

      if ((nn = neigh_find(p, &lt->fwaddr, 0)) != NULL)
      {
	nh = lt->fwaddr;
	nhi = nn->iface;
      }
      else
      {
	nh = nf2->n.nh;
	nhi = nf2->n.ifa;
      }
    }

    nfa.type = RTS_OSPF_EXT;
    nfa.capa = 0;
    nfa.metric1 = met1;
    nfa.metric2 = met2;
    nfa.oa = oa;
    nfa.ar = nf1->n.ar;
    nfa.nh = nh;
    nfa.ifa = nhi;
    nfa.tag = lt->tag;
    ri_install(po, ip, mlen, ORT_NET, &nfa);
  }

}

/* Add LSA into list of candidates in Dijkstra's algorithm */
static void
add_cand(list * l, struct top_hash_entry *en, struct top_hash_entry *par,
	 u16 dist, struct ospf_area *oa)
{
  node *prev, *n;
  int added = 0;
  struct top_hash_entry *act;

  if (en == NULL)
    return;
  if (en->lsa.age == LSA_MAXAGE)
    return;
  /* FIXME Does it have link back? Test it! */
  if (en->color == INSPF)
    return;

  if (dist >= en->dist)
    return;
  /*
   * FIXME The line above is not a bug, but we don't support multiple
   * next hops. I'll start as soon as nest will
   */
  DBG("     Adding candidate: rt: %I, id: %I, type: %u\n", en->lsa.rt,
      en->lsa.id, en->lsa.type);

  en->nhi = NULL;
  en->nh = IPA_NONE;

  calc_next_hop(en, par, oa);

  if (!en->nhi)
    return;			/* We cannot find next hop, ignore it */

  if (en->color == CANDIDATE)
  {				/* We found a shorter path */
    rem_node(&en->cn);
  }
  en->dist = dist;
  en->color = CANDIDATE;

  prev = NULL;

  if (EMPTY_LIST(*l))
  {
    add_head(l, &en->cn);
  }
  else
  {
    WALK_LIST(n, *l)
    {
      act = SKIP_BACK(struct top_hash_entry, cn, n);
      if ((act->dist > dist) ||
	  ((act->dist == dist) && (act->lsa.type == LSA_T_NET)))
      {
	if (prev == NULL)
	  add_head(l, &en->cn);
	else
	  insert_node(&en->cn, prev);
	added = 1;
	break;
      }
      prev = n;
    }

    if (!added)
    {
      add_tail(l, &en->cn);
    }
  }
  /* FIXME Some VLINK stuff should be here */
}

static void
calc_next_hop(struct top_hash_entry *en, struct top_hash_entry *par,
	      struct ospf_area *oa)
{
  struct ospf_neighbor *neigh;
  struct proto *p = &oa->po->proto;
  struct proto_ospf *po = oa->po;
  struct ospf_iface *ifa;
  u32 myrid = p->cf->global->router_id;

  DBG("     Next hop called.\n");
  if (ipa_equal(par->nh, IPA_NONE))
  {
    neighbor *nn;
    DBG("     Next hop calculating for id: %I rt: %I type: %u\n",
	en->lsa.id, en->lsa.rt, en->lsa.type);

    if (par == oa->rt)
    {
      if (en->lsa.type == LSA_T_NET)
      {
	if (en->lsa.rt == myrid)
	{
	  WALK_LIST(ifa, po->iface_list)
	    if (ipa_compare
		(ifa->iface->addr->ip, ipa_from_u32(en->lsa.id)) == 0)
	  {
	    en->nhi = ifa->iface;
	    return;
	  }
	  log(L_ERR "I didn't find interface for my self originated LSA!\n");
	  /* This could sometimes happen */
	  return;
	}
	else
	{
	  ip_addr ip = ipa_from_u32(en->lsa.id);
	  nn = neigh_find(p, &ip, 0);
	  if (nn)
	    en->nhi = nn->iface;
	  return;
	}
      }
      else
      {
	if ((neigh = find_neigh_noifa(po, en->lsa.rt)) == NULL)
	  return;
	en->nhi = neigh->ifa->iface;
	en->nh = neigh->ip;	/* Yes, neighbor is it's
				 * own next hop */
	return;
      }
    }
    if (par->lsa.type == LSA_T_NET)
    {
      if (en->lsa.type == LSA_T_NET)
	bug("Parent for net is net?");
      if ((en->nhi = par->nhi) == NULL)
	bug("Did not find next hop interface for INSPF lsa!");
      if ((neigh = find_neigh_noifa(po, en->lsa.rt)) == NULL)
	return;
      en->nhi = neigh->ifa->iface;
      en->nh = neigh->ip;	/* Yes, neighbor is it's own
				 * next hop */
      return;
    }
    else
    {				/* Parent is some RT neighbor */
      log(L_ERR "Router's parent has no next hop. (EN=%I, PAR=%I)",
	  en->lsa.id, par->lsa.id);
      /* I hoped this would never happen */
      return;
    }
  }
  en->nh = par->nh;
  en->nhi = par->nhi;
  DBG("     Next hop calculated: %I.\n", en->nh);
}

static void
rt_sync(struct proto_ospf *po)
{
  struct proto *p = &po->proto;
  struct fib_iterator fit;
  struct fib *fib = &po->rtf[ORT_NET];
  ort *nf;

  DBG("Now syncing my rt table with nest's\n");
  FIB_ITERATE_INIT(&fit, fib);
again:
  FIB_ITERATE_START(fib, &fit, nftmp)
  {
    nf = (ort *) nftmp;
    if (nf->n.metric1 == LSINFINITY)
    {
      net *ne;

      ne = net_get(p->table, nf->fn.prefix, nf->fn.pxlen);
      DBG("Deleting rt entry %I\n     (IP: %I, GW: %I)\n",
	  nf->fn.prefix, ip, nf->nh);
      rte_update(p->table, ne, p, NULL);

      /* Now delete my fib */
      FIB_ITERATE_PUT(&fit, nftmp);
      fib_delete(fib, nftmp);
      goto again;
    }
    else if (memcmp(&nf->n, &nf->o, sizeof(orta)))
    {				/* Some difference */
      net *ne;
      rta a0;
      rte *e;

      bzero(&a0, sizeof(a0));

      a0.proto = p;
      a0.source = nf->n.type;
      a0.scope = SCOPE_UNIVERSE;
      a0.cast = RTC_UNICAST;
      a0.dest = RTD_ROUTER;
      a0.flags = 0;
      a0.aflags = 0;
      a0.iface = nf->n.ifa;
      a0.gw = nf->n.nh;
      ne = net_get(p->table, nf->fn.prefix, nf->fn.pxlen);
      e = rte_get_temp(&a0);
      e->u.ospf.metric1 = nf->n.metric1;
      e->u.ospf.metric2 = nf->n.metric2;
      e->u.ospf.tag = nf->n.tag;
      e->pflags = 0;
      e->net = ne;
      e->pref = p->preference;
      DBG("Modifying rt entry %I\n     (IP: %I, GW: %I)\n",
	  nf->fn.prefix, ip, nf->nh);
      rte_update(p->table, ne, p, e);
    }
  }
  FIB_ITERATE_END(nftmp);
}
