/*
 *	BIRD -- OSPF
 *
 *	(c) 2000 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

void
init_stub_fib(struct fib_node *fn)
{
  struct stub_fib *sf=(struct stub_fib *)fn;

  sf->metric=LSINFINITY;
  sf->nhi=NULL;
}

void
ospf_rt_spfa(struct ospf_area *oa, struct proto *p)
{
  struct top_hash_entry *en, *nx;
  u32 i,*rts;
  struct ospf_lsa_rt *rt;
  struct ospf_lsa_rt_link *rtl,*rr;
  struct fib fib;
  struct stub_fib *sf;
  bird_clock_t delta;
  int age=0,flush=0;

  /* FIXME if I'm not in LOADING or EXCHANGE set flush=1 */
  if((delta=now-oa->lage)>=AGINGDELTA)
  {
     oa->lage=now;
     age=1;
  }

  WALK_SLIST_DELSAFE(SNODE en, nx, oa->lsal)	/* FIXME Make it DELSAFE */
  {
    en->color=OUTSPF;
    en->dist=LSINFINITY;
    if(age) ospf_age(en,delta,flush,p);
  }

  init_list(&oa->cand);		/* Empty list of candidates */
  oa->trcap=0;

  DBG("LSA db prepared, adding me into candidate list.\n");

  oa->rt->dist=0;
  oa->rt->color=CANDIDATE;
  add_head(&oa->cand, &oa->rt->cn);
  DBG("RT LSA: rt: %I, id: %I, type: %u\n",oa->rt->lsa.rt,oa->rt->lsa.id,oa->rt->lsa.type);

  while(!EMPTY_LIST(oa->cand))
  {
    struct top_hash_entry *act,*tmp;
    node *n;
    struct ospf_lsa_net *netw;

    n=HEAD(oa->cand);
    act=SKIP_BACK(struct top_hash_entry, cn, n);
    rem_node(n);

    DBG("Working on LSA: rt: %I, id: %I, type: %u\n",act->lsa.rt,act->lsa.id,act->lsa.type);

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
	    case LSART_VLNK:
	      DBG("Ignoring\n");
	      continue;
	      break;
	    case LSART_NET:
	      /* FIXME Oh shit so bad complication */
	      tmp=ospf_hash_find(oa->gr,rtl->id,rtl->id,LSA_T_NET);
	      if(tmp==NULL) DBG("Fuck!\n");
	      else DBG("Found. :-)\n");
	      break;
	    case LSART_PTP: /* FIXME */
	      tmp=ospf_hash_find(oa->gr,rtl->id,rtl->id,LSA_T_RT);
	      DBG("PTP searched.\n");
	      break;
	    default:
	      log("Unknown link type in router lsa.\n");
	      break;
	  }
	  add_cand(&oa->cand,tmp,act,act->dist+rtl->metric,p,oa);
	}
        break;
      case LSA_T_NET:
	netw=(struct ospf_lsa_net *)act->lsa_body;
	rts=(u32 *)(netw+1);
	for(i=0;i<(act->lsa.length-sizeof(struct ospf_lsa_header)-
	  sizeof(struct ospf_lsa_net))/sizeof(u32);i++)
	{
	  DBG("     Working on router %I ",*(rts+i));
	  tmp=ospf_hash_find(oa->gr, *(rts+i), *(rts+i), LSA_T_RT);
	  if(tmp!=NULL) DBG("Found :-)\n");
	  else DBG("Fuck!\n");
          add_cand(&oa->cand,tmp,act,act->dist,p,oa);
	}
        break;
    }
    /* FIXME Now modify rt for this entry */
    if((act->lsa.type==LSA_T_NET)&&(act->nhi!=NULL))
    {
      net *ne;
      rta a0;
      rte *e;
      ip_addr ip;
      struct ospf_lsa_net *ln=act->lsa_body;

      bzero(&a0, sizeof(a0));

      a0.proto=p;
      a0.source=RTS_OSPF;
      a0.scope=SCOPE_UNIVERSE;	/* What's this good for? */
      a0.cast=RTC_UNICAST;
      a0.dest=RTD_ROUTER;
      a0.flags=0;
      a0.aflags=0;
      a0.iface=act->nhi;
      a0.gw=act->nh;
      a0.from=act->nh;		/* FIXME Just a test */
      ip=ipa_and(ipa_from_u32(act->lsa.id),ln->netmask);
      ne=net_get(p->table, ip, ipa_mklen(ln->netmask));
      e=rte_get_temp(&a0);
      e->u.ospf.metric1=act->dist;
      e->u.ospf.metric2=0;
      e->u.ospf.tag=0;			/* FIXME Some config? */
      e->pflags = 0;
      e->net=ne;
      DBG("Modifying rt entry %I mask %I\n     (IP: %I, GW: %I, Iface: %s)\n",
        act->lsa.id,ln->netmask,ip,act->nh,act->nhi->name);
      rte_update(p->table, ne, p, e);
    }
    else
    {
      if(act->lsa.type==LSA_T_NET)
      {
        struct ospf_lsa_net *ln=act->lsa_body;
        DBG("NOT modifying rt entry %I mask %I\n",act->lsa.id,ln->netmask);
      }
    }
  }

  DBG("Now calculating routes for stub networks.\n");

  /* Now calculate routes to stub networks */
  fib_init(&fib,p->pool,sizeof(struct stub_fib),16,init_stub_fib);
    /*FIXME 16? */

  WALK_SLIST_DELSAFE(SNODE en, SNODE nx, oa->lsal)
  {
    if((en->lsa.type==LSA_T_RT)||(en->lsa.type==LSA_T_NET))
    {
      if(en->dist==LSINFINITY)
      {
        /* FIXME I cannot remove node If I'm not FULL states */
        //s_rem_node(SNODE en);
	/* FIXME Remove from routing table! */
	//mb_free(en->lsa_body);
	//ospf_hash_delete(oa->gr, en);
      }
      if(en->lsa.type==LSA_T_RT)
      {
        ip_addr ip;

        DBG("Working on LSA: rt: %I, id: %I, type: %u\n",en->lsa.rt,en->lsa.id,en->lsa.type);
        rt=(struct ospf_lsa_rt *)en->lsa_body;
	if((rt->VEB)&(1>>LSA_RT_V)) oa->trcap=1;
	rr=(struct ospf_lsa_rt_link *)(rt+1);
	for(i=0;i<rt->links;i++)
	{
	  rtl=rr+i;
	  if(rtl->type==LSART_STUB)
	  {
	    DBG("       Working on stub network: %I\n",rtl->id);
	    ip=ipa_from_u32(rtl->id);
	    /* Check destination and so on (pg 166) */
	    sf=fib_get(&fib,&ip,
	      ipa_mklen(ipa_from_u32(rtl->data)));

	    if(sf->metric>(en->dist+rtl->metric))
	    {
	      sf->metric=en->dist+rtl->metric;
	      calc_next_hop_fib(en,sf,p,oa);
	      if(sf->nhi!=NULL)
	      {
                net *ne;
                rta a0;
                rte *e;

                bzero(&a0, sizeof(a0));
           
                a0.proto=p;
                a0.source=RTS_OSPF;
                a0.scope=SCOPE_UNIVERSE;	/* What's this good for? */
                a0.cast=RTC_UNICAST;
                a0.dest=RTD_ROUTER;
                a0.flags=0;
                a0.aflags=0;
                a0.iface=sf->nhi;
                a0.gw=sf->nh;
                a0.from=sf->nh;		/* FIXME Just a test */
                ip=ipa_from_u32(rtl->id);
                ne=net_get(p->table, ip, ipa_mklen(ipa_from_u32(rtl->data)));
                e=rte_get_temp(&a0);
                e->u.ospf.metric1=sf->metric;
                e->u.ospf.metric2=0;
                e->u.ospf.tag=0;			/* FIXME Some config? */
                e->pflags = 0;
                e->net=ne;
                DBG("Modifying stub rt entry %I mask %I\n     (GW: %I, Iface: %s)\n",
                  ip,rtl->data,sf->nh,sf->nhi->name);
                rte_update(p->table, ne, p, e);
	      }
	    }
	  }
	}
      }
    }
  }
}

void
add_cand(list *l, struct top_hash_entry *en, struct top_hash_entry *par, 
  u16 dist, struct proto *p, struct ospf_area *oa)
{
  node *prev,*n;
  int flag=0,added=0;
  struct top_hash_entry *act;

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
  
  calc_next_hop(par,en,p,oa);

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
  struct proto *p, struct ospf_area *oa)
{
  struct ospf_neighbor *neigh;
  struct proto_ospf *po=(struct proto_ospf *)p;
  DBG("     Next hop called\n");
  if(par==oa->rt) return;
  if(par->nhi==NULL)
  {
    neighbor *nn;
    DBG("     Next hop calculating for id: %I rt: %I type: %u\n",en->lsa.id,en->lsa.rt,en->lsa.type);
    if(par->lsa.type!=LSA_T_RT) return;
    if((neigh=find_neigh_noifa(po,en->lsa.rt))==NULL) return;
    nn=neigh_find(p,&neigh->ip,0);
    DBG("     Next hop calculated: %I\n", nn->addr);
    en->nh=nn->addr;
    en->nhi=nn->iface;
    return;
  }
  en->nh=par->nh;
  en->nhi=par->nhi;
  DBG("     Next hop calculated: %I\n", en->nh);
}

void
calc_next_hop_fib(struct top_hash_entry *par, struct stub_fib *en,
  struct proto *p, struct ospf_area *oa)
{
  struct ospf_neighbor *neigh;
  struct proto_ospf *po=(struct proto_ospf *)p;
  DBG("     Next hop called\n");
  if(par==oa->rt) return;
  if(par->nhi==NULL)
  {
    neighbor *nn;
    DBG("     Next hop calculating for Fib\n");
    if(par->lsa.type!=LSA_T_RT) return;
    if((neigh=find_neigh_noifa(po,par->lsa.rt))==NULL) return;
    nn=neigh_find(p,&neigh->ip,0);
    DBG("     Next hop calculated: %I\n", nn->addr);
    en->nh=nn->addr;
    en->nhi=nn->iface;
    return;
  }
  en->nh=par->nh;
  en->nhi=par->nhi;
  DBG("     Next hop calculated: %I\n", en->nh);
}
