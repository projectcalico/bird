/*
 *	BIRD -- OSPF
 *
 *	(c) 2000 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

void
ospf_rt_spfa(struct ospf_area *oa, struct proto *p)
{
  struct top_hash_entry *en, *nx;
  u32 i,*rts;
  struct ospf_lsa_rt *rt;
  struct ospf_lsa_rt_link *rtl;
  rta a0,*a;

  /*
   * First of all, mark all vertices as they are not in SPF
   * Maybe I can join this work with Aging of structure
   * FIXME look at it
   */

  WALK_SLIST(SNODE en, oa->lsal)
  {
    en->color=OUTSPF;
    en->dist=LSINFINITY;
  }

  init_list(&oa->cand);		/* Empty list of candidates */
  oa->trcap=0;

  oa->rt->dist=0;
  oa->rt->color=CANDIDATE;
  add_head(&oa->cand, &en->cn);

  while(!EMPTY_LIST(oa->cand))
  {
    struct top_hash_entry *act,*tmp;
    node *n;
    struct ospf_lsa_net *netw;

    n=HEAD(oa->cand);
    act=SKIP_BACK(struct top_hash_entry, cn, n);
    rem_node(n);

    act->color=INSPF;
    switch(act->lsa.type)
    {
      case LSA_T_RT:
        rt=(struct ospf_lsa_rt *)act->lsa_body;
	if((rt->VEB)&(1>>LSA_RT_V)) oa->trcap=1;
	rtl=(struct ospf_lsa_rt_link *)(rt+1);
	for(i=0;rt->links;i++)
	{
	  tmp=NULL;
	  switch((rtl+i)->type)
	  {
            case LSART_STUB:
	    case LSART_VLNK:
	      continue;
	      break;
	    case LSART_NET:
	      tmp=ospf_hash_find(oa->gr,rtl->data,rtl->id,LSA_T_RT);
	      break;
	    case LSART_PTP:
	      tmp=ospf_hash_find(oa->gr,rtl->data,rtl->id,LSA_T_NET);
	      break;
	    default:
	      log("Unknown link type in router lsa.\n");
	      break;
	  }
	  add_cand(&oa->cand,tmp,act,act->dist+rtl->metric,p);
	}
        break;
      case LSA_T_NET:
	netw=(struct ospf_lsa_net *)act->lsa_body;
	rts=(u32 *)(netw+1);
	for(i=0;i<(act->lsa.length-sizeof(struct ospf_lsa_header)-
	  sizeof(struct ospf_lsa_net))/sizeof(u32);i++)
	{
	  tmp=ospf_hash_find(oa->gr, *rts, *rts, LSA_T_RT);
          add_cand(&oa->cand,tmp,act,act->dist,p);
	}
        break;
    }
    /* FIXME Now modify rt for this entry */
    if((act->lsa.type==LSA_T_NET)&&(act->nhi!=NULL))
    {
      net *ne;
      rte *e;
      ip_addr ip;
      struct ospf_lsa_net *ln=en->lsa_body;

      a0.proto=p;
      a0.source=RTS_OSPF;
      a0.scope=SCOPE_UNIVERSE;	/* What's this good for? */
      a0.cast=RTC_UNICAST;
      a0.dest=RTD_ROUTER;	/* FIXME */
      a0.flags=0;
      a0.iface=act->nhi;
      a0.gw=act->nh;
      ip=ipa_from_u32(act->lsa.id);


      ne=net_get(p->table, ip, ipa_mklen(ln->netmask));
      e=rte_get_temp(&a0);
      e->u.ospf.metric1=en->dist;
      e->u.ospf.metric2=0;
      e->u.ospf.tag=0;			/* FIXME Some config? */

      rte_update(p->table, ne, p, e);

      //a0.from=act->lsa.rt;
    }




  }

  /* Now calculate routes to stub networks */

  WALK_SLIST_DELSAFE(SNODE en, SNODE nx, oa->lsal)
  {
    if((en->lsa.type==LSA_T_RT)||(en->lsa.type==LSA_T_NET))
    {
      if(en->dist==LSINFINITY)
      {
        s_rem_node(SNODE en);
	/* FIXME Remove from routing table! */
	mb_free(en->lsa_body);
	ospf_hash_delete(oa->gr, en);
      }
      if(en->lsa.type==LSA_T_NET)
      {
        rt=(struct ospf_lsa_rt *)en->lsa_body;
	if((rt->VEB)&(1>>LSA_RT_V)) oa->trcap=1;
	rtl=(struct ospf_lsa_rt_link *)(rt+1);
	for(i=0;rt->links;i++)
	{
	  if((rtl+i)->type==LSART_STUB)
	  {
	    /* Check destination and so on (pg 166) */
	  }
	}
      }
    }
  }
}

void
add_cand(list *l, struct top_hash_entry *en, struct top_hash_entry *par, 
  u16 dist, struct proto *p)
{
  node *prev,*n;
  int flag=0;
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

  en->nhi=NULL;
  
  calc_next_hop(par,en,p);

  if(en->color==CANDIDATE)	/* We found shorter path */
  {
    rem_node(&en->cn);
  }

  en->dist=dist;
  en->color=CANDIDATE;

  prev=NULL;

  WALK_LIST(n,*l)
  {
    act=SKIP_BACK(struct top_hash_entry, cn, n);
    if((act->dist>dist)||
      ((act->dist==dist)&&(act->lsa.type==LSA_T_NET)))
    {
      if(prev==NULL) add_head(l,&en->cn);
      else insert_node(&en->cn,prev);
      break;
    }
    prev=n;
  }
  /* FIXME Some VLINK staff should be here */
  
}

void
calc_next_hop(struct top_hash_entry *par, struct top_hash_entry *en,
  struct proto *p)
{
  struct ospf_neighbor *neigh;
  struct proto_ospf *po=(struct proto_ospf *)p;
  if(par->nhi==NULL)
  {
    neighbor *nn;
    if(par->lsa.type!=LSA_T_RT) return;
    if((neigh=find_neigh_noifa(po,en->lsa.rt))==NULL) bug("XXX\n");
    nn=neigh_find(p,&neigh->ip,0);
    en->nh=nn->addr;
    en->nhi=nn->iface;
  }
  en->nh=par->nh;
  en->nhi=par->nhi;
}
