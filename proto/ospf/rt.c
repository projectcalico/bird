/*
 *	BIRD -- OSPF
 *
 *	(c) 2000 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

void
init_infib(struct fib_node *fn)
{
  struct infib *f=(struct infib *)fn;

  f->metric=LSINFINITY;
  f->en=NULL;
}

void
init_efib(struct fib_node *fn)
{
  struct extfib *f=(struct extfib *)fn;

  f->metric=LSINFINITY;
  f->metric2=LSINFINITY;
  f->nh=ipa_from_u32(0);
  f->nhi=NULL;
}

/**
 * ospf_rt_spfa - calculate internal routes
 * @oa: OSPF area
 *
 * Calculation of internal paths in area is described in 16.1 of RFC 2328.
 * It's based on Dijkstra shortest path tree algorithmus.
 * RFC recommends to add ASBR routers into routing table. I don't do this
 * and latter parts of routing table calculation looks directly into LSA
 * Database. This function is invoked from area_disp().
 */
void
ospf_rt_spfa(struct ospf_area *oa)
{
  struct top_hash_entry *en;
  u32 i,*rts;
  struct ospf_lsa_rt *rt;
  struct ospf_lsa_rt_link *rtl,*rr;
  struct fib *in=&oa->infib;
  struct infib *nf;
  struct fib_iterator fit;
  int age=0,flush=0;
  struct proto *p=&oa->po->proto;
  struct proto_ospf *po=oa->po;
  ip_addr ip;
  struct ospf_lsa_net *ln;

  OSPF_TRACE(D_EVENTS, "Starting routing table calculation for area %I",
    oa->areaid);

  if(oa->rt==NULL) return;

  FIB_WALK(in,nftmp)
  {
    nf=(struct infib *)nftmp;
    nf->metric=LSINFINITY;
    nf->en=NULL;
  }
  FIB_WALK_END;

  init_list(&oa->cand);		/* Empty list of candidates */
  oa->trcap=0;

  DBG("LSA db prepared, adding me into candidate list.\n");

  oa->rt->dist=0;
  oa->rt->color=CANDIDATE;
  add_head(&oa->cand, &oa->rt->cn);
  DBG("RT LSA: rt: %I, id: %I, type: %u\n",oa->rt->lsa.rt,
    oa->rt->lsa.id,oa->rt->lsa.type);

  while(!EMPTY_LIST(oa->cand))
  {
    struct top_hash_entry *act,*tmp;
    node *n;
    u16 met;

    n=HEAD(oa->cand);
    act=SKIP_BACK(struct top_hash_entry, cn, n);
    rem_node(n);

    DBG("Working on LSA: rt: %I, id: %I, type: %u\n",act->lsa.rt,
      act->lsa.id,act->lsa.type);

    act->color=INSPF;
    switch(act->lsa.type)
    {
      case LSA_T_RT:
        rt=(struct ospf_lsa_rt *)act->lsa_body;
	if((rt->VEB)&(1>>LSA_RT_V)) oa->trcap=1;
	rr=(struct ospf_lsa_rt_link *)(rt+1);
	DBG("  Number of links: %u\n",rt->links);
	for(i=0;i<rt->links;i++)
	{
	  tmp=NULL;
	  rtl=(rr+i);
	  DBG("     Working on link: %I (type: %u)  ",rtl->id,rtl->type);
	  switch(rtl->type)
	  {
            case LSART_STUB:
	     DBG("\n");
	     ip=ipa_from_u32(rtl->id);
	     nf=fib_get(in,&ip, ipa_mklen(ipa_from_u32(rtl->data)));
	     if(nf->metric>(met=act->dist+rtl->metric))
	     {
               DBG("       Adding stub route....\n");
               if(oa->rt==act) break;
               if(act->nhi==NULL) break;
	       nf->metric=met;
	       nf->en=act;
               DBG("            Adding stub route: %I\n",ip);
               DBG("            Next hop=%I\n",nf->en->nh);
	     }
             else DBG("            NOT adding stub route: %I\n",ip);
	     break;
	    case LSART_VLNK:
	      DBG("Ignoring\n");
	      continue;
	      break;
	    case LSART_NET:
	      tmp=ospf_hash_find(oa->gr,rtl->id,rtl->id,LSA_T_NET);
	      if(tmp==NULL) DBG("Fuck!\n");
	      else DBG("Found. :-)\n");
	      break;
	    case LSART_PTP:
	      tmp=ospf_hash_find(oa->gr,rtl->id,rtl->id,LSA_T_RT);
	      DBG("PTP searched.\n");
	      break;
	    default:
	      log("Unknown link type in router lsa.");
	      break;
	  }
          if(tmp) DBG("Going to add cand, Mydist: %u, Req: %u\n",
            tmp->dist, act->dist+rtl->metric);
	  add_cand(&oa->cand,tmp,act,act->dist+rtl->metric,oa);
	}
        break;
      case LSA_T_NET:
        ln=act->lsa_body;
	ip=ipa_and(ipa_from_u32(act->lsa.id),ln->netmask);
        nf=fib_get(in,&ip, ipa_mklen(ln->netmask));
	if(nf->metric>act->dist)
	{
	  nf->metric=act->dist;
	  nf->en=act;
          DBG("    Adding into routing table\n");
	}
	rts=(u32 *)(ln+1);
	for(i=0;i<(act->lsa.length-sizeof(struct ospf_lsa_header)-
	  sizeof(struct ospf_lsa_net))/sizeof(u32);i++)
	{
	  DBG("     Working on router %I ", *(rts+i));
	  tmp=ospf_hash_find(oa->gr, *(rts+i), *(rts+i), LSA_T_RT);
	  if(tmp!=NULL) DBG("Found :-)\n");
	  else DBG("Fuck!\n");
          add_cand(&oa->cand,tmp,act,act->dist,oa);
	}
        break;
    }
  }
  /* Now sync our fib with nest's */
  DBG("Now syncing my rt table with nest's\n");
  FIB_ITERATE_INIT(&fit,in);
again:
  FIB_ITERATE_START(in,&fit,nftmp)
  {
    nf=(struct infib *)nftmp;
    if(nf->metric==LSINFINITY) 
    {
      net *ne;
      struct top_hash_entry *en=nf->en;
      ln=en->lsa_body;
  
      ne=net_get(p->table, nf->fn.prefix, nf->fn.pxlen);
      if((en!=NULL)&&(en->nhi!=NULL))
        DBG("Deleting rt entry %I\n     (P: %x, GW: %I, Iface: %s)\n",
        nf->fn.prefix, en, en->nh,en->nhi->name);
      rte_update(p->table, ne, p, NULL);

      /* Now delete my fib */
      FIB_ITERATE_PUT(&fit, nftmp);
      fib_delete(in, nftmp);
      goto again;
    }
    else
    {
      /* Update routing table */
      if(nf->en->nhi==NULL)
      {
        struct top_hash_entry *en=nf->en;
        struct ospf_neighbor *neigh;
        neighbor *nn;

        if((neigh=find_neigh_noifa(po,en->lsa.rt))==NULL)
	{
	  goto skip;
	}
        nn=neigh_find(p,&neigh->ip,0);
        DBG("     Next hop calculated: %I\n", nn->addr);
        en->nh=nn->addr;
        en->nhi=nn->iface;
      }

      {
        net *ne;
        rta a0;
        rte *e;
	struct top_hash_entry *en=nf->en;
        ln=en->lsa_body;
  
        bzero(&a0, sizeof(a0));
  
        a0.proto=p;
        a0.source=RTS_OSPF;
        a0.scope=SCOPE_UNIVERSE;
        a0.cast=RTC_UNICAST;
        if(ipa_to_u32(en->nh)==0) a0.dest=RTD_DEVICE;
        else a0.dest=RTD_ROUTER;
        a0.flags=0;
        a0.aflags=0;
        a0.iface=en->nhi;
        a0.gw=en->nh;
        ne=net_get(p->table, nf->fn.prefix, nf->fn.pxlen);
        e=rte_get_temp(&a0);
        e->u.ospf.metric1=nf->metric;
        e->u.ospf.metric2=LSINFINITY;
        e->u.ospf.tag=0;
        e->pflags = 0;
        e->net=ne;
	e->pref = p->preference;
        DBG("Modifying rt entry %I\n     (GW: %I, Iface: %s)\n",
          nf->fn.prefix,en->nh,en->nhi->name);
        rte_update(p->table, ne, p, e);
      }
    }

  }
skip:
  FIB_ITERATE_END(nftmp);
  ospf_ext_spfa(po);
}

/**
 * ospf_ext_spfa - calculate external paths
 * @po: protocol
 *
 * After routing table for any area is calculated, calculation of external
 * path is invoked. This process is described in 16.6 of RFC 2328.
 * Inter- and Intra-area paths are always prefered over externals.
 */
void
ospf_ext_spfa(struct proto_ospf *po)	/* FIXME looking into inter-area */
{
  struct top_hash_entry *en,*etmp,*absr;
  struct fib *ef=&po->efib;
  struct extfib *nf;
  struct fib_iterator fit;
  struct ospf_area *oa=NULL,*atmp,*absroa;
  struct proto *p=&po->proto;
  struct ospf_lsa_ext *le;
  struct ospf_lsa_ext_tos *lt;
  int mlen;
  ip_addr ip,nnh;
  struct iface *nnhi=NULL;
  u16 met,met2;
  u32 tag;
  neighbor *nn;

  OSPF_TRACE(D_EVENTS,"Starting routing table calculation for ext routes");

  FIB_WALK(ef,nftmp)
  {
    nf=(struct extfib *)nftmp;
    nf->metric=LSINFINITY;
    nf->metric2=LSINFINITY;
  }
  FIB_WALK_END;

  WALK_LIST(oa,po->area_list)
  {
    if(!oa->stub) break;
  }

  if(oa==NULL) return;

  WALK_SLIST(en,oa->lsal)
  {
    if(en->lsa.type!=LSA_T_EXT) continue;
    if(en->lsa.age==LSA_MAXAGE) continue;
    if(en->lsa.rt==p->cf->global->router_id) continue;

    le=en->lsa_body;
    lt=(struct ospf_lsa_ext_tos *)(le+1);

    DBG("%s: Working on LSA. ID: %I, RT: %I, Type: %u, Mask %I\n",
        p->name,en->lsa.id,en->lsa.rt,en->lsa.type,le->netmask);

    if(lt->metric==LSINFINITY) continue;
    ip=ipa_and(ipa_from_u32(en->lsa.id),le->netmask);
    mlen=ipa_mklen(le->netmask);
    if((mlen<0)||(mlen>32))
    {
      log("%s: Invalid mask in LSA. ID: %I, RT: %I, Type: %u, Mask %I",
        p->name,en->lsa.id,en->lsa.rt,en->lsa.type,le->netmask);
      continue;
    }

    nf=NULL;

    WALK_LIST(atmp,po->area_list)
    {
      if((nf=fib_find(&atmp->infib,&ip, mlen))!=NULL) continue;
      /* Some intra area path exists */
    }

    absr=NULL;
    absroa=NULL;
    nnhi=NULL;
    nnh=IPA_NONE;

    met=0;met2=0;tag=0;

    WALK_LIST(atmp,po->area_list)
    {
      if((etmp=ospf_hash_find(atmp->gr,en->lsa.rt,en->lsa.rt,LSA_T_RT))!=NULL)
      {
        if((absr==NULL) || (absr->dist>etmp->dist) ||
          ((etmp->dist==absr->dist) && (absroa->areaid<atmp->areaid)))
        {
          absr=etmp;
          absroa=atmp;
	  break;
        }
      }
    }
    if((absr==NULL)||(absr->dist==LSINFINITY))
    {
      DBG("ABSR is null or its dist=INF\n");
      continue;
    }

    if(ipa_compare(lt->fwaddr,ipa_from_u32(0))==0)
    {
      if(lt->etos>0)
      {
        met=absr->dist;
        met2=lt->metric;
      }
      else
      {
        met=absr->dist+lt->metric;
	met2=LSINFINITY;
      }
      tag=lt->tag;
    }
    else
    {
      nf=NULL;
      WALK_LIST(atmp,po->area_list)
      {
        if((nf=fib_route(&atmp->infib,lt->fwaddr,32))!=NULL)
	{
	  break;
	}
      }

      if(nf==NULL)
      {
        DBG("Cannot find network route (GW=%I)\n",lt->fwaddr);
        continue;
      }

      if(lt->etos>0)
      {
        met=nf->metric;
        met2=lt->metric;
      }
      else
      {
        met=nf->metric+lt->metric;
	met2=LSINFINITY;
      }
      tag=lt->tag;

      if((nn=neigh_find(p,&lt->fwaddr,0))!=NULL)
      {
        nnh=nn->addr;
        nnhi=nn->iface;
      }
    }

    nf=fib_get(ef,&ip, mlen);
    if((nf->metric>met) || ((nf->metric==met)&&(nf->metric2>met2)))
    {
      nf->metric=met;
      nf->metric2=met2;
      nf->tag=tag;

      if(ipa_compare(nnh,ipa_from_u32(0))!=0)
      {
        nf->nh=nnh;
        nf->nhi=nnhi;
      }
      else
      {
        if(ipa_compare(absr->nh,ipa_from_u32(0))==0)
        {
          struct ospf_neighbor *neigh;

          if((neigh=find_neigh_noifa(po,absr->lsa.rt))==NULL)
	  {
             DBG("Cannot find neighbor\n");
	     continue;
	  }
          nn=neigh_find(p,&neigh->ip,0);
          DBG("     Next hop calculated: %I\n", nn->addr);
          nf->nh=nn->addr;
          nf->nhi=nn->iface;
        }
        else
        {
          nf->nh=absr->nh;
	  nf->nhi=absr->nhi;
        }
      }
    }
  }

  DBG("Now syncing my rt table with nest's\n");
  FIB_ITERATE_INIT(&fit,ef);
noch:
  FIB_ITERATE_START(ef,&fit,nftmp)
  {
    nf=(struct extfib *)nftmp;
    if(nf->metric==LSINFINITY) 
    {
      net *ne;
  
      ne=net_get(p->table, nf->fn.prefix, nf->fn.pxlen);
      DBG("Deleting rt entry %I\n     (IP: %I, GW: %I)\n",
        nf->fn.prefix,ip,nf->nh);
      rte_update(p->table, ne, p, NULL);

      /* Now delete my fib */
      FIB_ITERATE_PUT(&fit, nftmp);
      fib_delete(ef, nftmp);
      goto noch;
    }
    else
    {
      net *ne;
      rta a0;
      rte *e;
  
      bzero(&a0, sizeof(a0));
  
      a0.proto=p;
      a0.source=RTS_OSPF_EXT;
      a0.scope=SCOPE_UNIVERSE;
      a0.cast=RTC_UNICAST;
      a0.dest=RTD_ROUTER;
      a0.flags=0;
      a0.aflags=0;
      a0.iface=nf->nhi;
      a0.gw=nf->nh;
      ne=net_get(p->table, nf->fn.prefix, nf->fn.pxlen);
      e=rte_get_temp(&a0);
      e->u.ospf.metric1=nf->metric;
      e->u.ospf.metric2=nf->metric2;
      e->u.ospf.tag=nf->tag;
      e->pflags = 0;
      e->net=ne;
      e->pref = p->preference;
      DBG("Modifying rt entry %I\n     (IP: %I, GW: %I)\n",
        nf->fn.prefix,ip,nf->nh);
      rte_update(p->table, ne, p, e);
    }
  }
let:
  FIB_ITERATE_END(nftmp);
}

void
add_cand(list *l, struct top_hash_entry *en, struct top_hash_entry *par, 
  u16 dist, struct ospf_area *oa)
{
  node *prev,*n;
  int flag=0,added=0;
  struct top_hash_entry *act;
  struct proto *p=&oa->po->proto;

  if(en==NULL) return;
  if(en->lsa.age==LSA_MAXAGE) return;
  /* FIXME Does it have link back? Test it! */
  if(en->color==INSPF) return;

  if(dist>=en->dist) return;
  /*
   * FIXME The line above is not a bug, but we don't support
   * multiple next hops. I'll start as soon as nest will
   */
  DBG("     Adding candidate: rt: %I, id: %I, type: %u\n",en->lsa.rt,en->lsa.id,en->lsa.type);

  en->nhi=NULL;
  
  calc_next_hop(par,en,oa);

  if(en->color==CANDIDATE)	/* We found a shorter path */
  {
    rem_node(&en->cn);
  }

  en->dist=dist;
  en->color=CANDIDATE;

  prev=NULL;

  if(EMPTY_LIST(*l))
  {
    add_head(l,&en->cn);
  }
  else
  {
    WALK_LIST(n,*l)
    {
      act=SKIP_BACK(struct top_hash_entry, cn, n);
      if((act->dist>dist)||
        ((act->dist==dist)&&(act->lsa.type==LSA_T_NET)))
      {
        if(prev==NULL) add_head(l,&en->cn);
        else insert_node(&en->cn,prev);
	added=1;
        break;
      }
      prev=n;
    }

    if(!added)
    {
      add_tail(l,&en->cn);
    }
  }
  /* FIXME Some VLINK staff should be here */
}

void
calc_next_hop(struct top_hash_entry *par, struct top_hash_entry *en,
  struct ospf_area *oa)
{
  struct ospf_neighbor *neigh;
  struct proto *p=&oa->po->proto;
  struct proto_ospf *po=oa->po;
  struct ospf_iface *ifa;
  u32 myrid=p->cf->global->router_id;

  DBG("     Next hop called\n");
  if(ipa_to_u32(par->nh)==0)
  {
    neighbor *nn;
    DBG("     Next hop calculating for id: %I rt: %I type: %u\n",
      en->lsa.id,en->lsa.rt,en->lsa.type);

    if(par==oa->rt)
    {
      if(en->lsa.type==LSA_T_NET)
      {
        if(en->lsa.rt==myrid)
        {
          WALK_LIST(ifa,po->iface_list)
            if(ipa_compare(ifa->iface->addr->ip,ipa_from_u32(en->lsa.id))==0)
            {
              en->nhi=ifa->iface;
              return;
            }
          bug("I didn't find interface for my self originated LSA!\n");
        }
        else
        {
          ip_addr ip=ipa_from_u32(en->lsa.id);
          nn=neigh_find(p,&ip,0);
          if(nn) en->nhi=nn->iface;
          return;
        }
      }
      else
      {
        if((neigh=find_neigh_noifa(po,en->lsa.rt))==NULL) return;
        en->nhi=neigh->ifa->iface;
        return;
      }
    }

    if(par->lsa.type==LSA_T_NET)
    {
      if(en->lsa.type==LSA_T_NET) bug("Parent for net is net?");
      en->nhi=par->nhi;
      if((neigh=find_neigh_noifa(po,en->lsa.rt))==NULL) return;
      en->nh=neigh->ip;
      return;
    }
    else
    {
      if((neigh=find_neigh_noifa(po,par->lsa.rt))==NULL) return;
      nn=neigh_find(p,&neigh->ip,0);
      if(nn)
      {
        en->nhi=nn->iface;
        en->nh=neigh->ip;
      }
      return;
    }
  }
  en->nh=par->nh;
  en->nhi=par->nhi;
  DBG("     Next hop calculated: %I..\n", en->nh);
}

