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
    struct ospf_lsa_net *net;

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
	  add_cand(&oa->cand,tmp,act,act->dist+rtl->metric);
	}
        break;
      case LSA_T_NET:
	net=(struct ospf_lsa_net *)act->lsa_body;
	rts=(u32 *)(net+1);
	for(i=0;i<(act->lsa.length-sizeof(struct ospf_lsa_header)-
	  sizeof(struct ospf_lsa_net))/sizeof(u32);i++)
	{
	  tmp=ospf_hash_find(oa->gr, *rts, *rts, LSA_T_RT);
          add_cand(&oa->cand,tmp,act,act->dist);
	}
        break;
    }
    /* FIXME Now modify rt for this entry */
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
  u16 dist)
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
  
  en->nh=calc_next_hop(par);

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

struct top_hash_entry *
calc_next_hop(struct top_hash_entry *par)
{
  if(par->nh==NULL)
  {
    if(par->lsa.type!=LSA_T_RT) return NULL;
  }
  return par;
}
