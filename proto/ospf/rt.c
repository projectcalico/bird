/*
 *	BIRD -- OSPF
 *
 *	(c) 2000 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

/* FIXME next hop calculation
 * FIXME sync with BIRD's routing table
 */

void
ospf_rt_spfa(struct ospf_area *oa, struct proto *p)
{
  struct top_hash_entry *en, *nx;
  slab *sl, *sll;
  struct spf_n *cn;
  u32 i,*rts;
  struct ospf_lsa_rt *rt;
  struct ospf_lsa_rt_link *rtl;

  /*
   * First of all, mark all vertices as they are not in SPF
   * Maybe I can join this work with Aging of structure
   */

  WALK_SLIST(SNODE en, oa->lsal)
  {
    en->color=OUTSPF;
    en->dist=LSINFINITY;
  }

  init_list(&oa->cand);		/* Empty list of candidates */
  oa->trcap=0;

  sl=sl_new(p->pool,sizeof(struct spf_n));
  sll=sl_new(p->pool,sizeof(list));

  cn=sl_alloc(sl);
  cn->en=oa->rt;
  oa->rt->dist=0;
  oa->rt->color=CANDIDATE;
  add_head(&oa->cand,NODE cn);

  while(!EMPTY_LIST(oa->cand))
  {
    struct top_hash_entry *act,*tmp;
    node *n;
    struct ospf_lsa_net *net;

    n=HEAD(oa->cand);
    act=((struct spf_n *)n)->en;
    rem_node(n);
    sl_free(sl,n);		/* Good idea? */

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
	  add_cand(&oa->cand,tmp,act,act->dist+rtl->metric,sl,sll);
	}
        break;
      case LSA_T_NET:
	net=(struct ospf_lsa_net *)act->lsa_body;
	rts=(u32 *)(net+1);
	for(i=0;i<(act->lsa.length-sizeof(struct ospf_lsa_header)-
	  sizeof(struct ospf_lsa_net))/sizeof(u32);i++)
	{
	  tmp=ospf_hash_find(oa->gr, *rts, *rts, LSA_T_RT);
          add_cand(&oa->cand,tmp,act,act->dist,sl,sll);
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
  u16 dist, slab *s, slab *sll)
{
  struct spf_n *tmp;
  node *prev;
  int flag=0;

  if(en==NULL) return;
  if(en->lsa.age==LSA_MAXAGE) return;
  /* FIXME Does it have link back? Test it! */
  if(en->color==INSPF) return;

  if(dist>en->dist) return;
  
  if(dist==en->dist)
  {
    en->nh=multi_next_hop(par,en,s,sll);
  }
  else
  {
    en->nh=calc_next_hop(par,s,sll);

    if(en->color==CANDIDATE)
    {
      WALK_LIST(tmp,*l)
      {
        if(tmp->en==en)
	{
	  rem_node(NODE tmp);
	  flag=1;
	  break;
	}
      }
    }

    if(flag!=1)
    {
      tmp=sl_alloc(s);
      tmp->en=en;
    }

    en->dist=dist;
    en->color=CANDIDATE;

    prev=NULL;

    WALK_LIST(tmp,*l)
    {
      if((tmp->en->dist>dist)||
        ((tmp->en->dist==dist)&&(tmp->en->lsa.type==LSA_T_NET)))
      {
        if(prev==NULL) add_head(l,NODE tmp);
	else insert_node(NODE tmp,prev);
        break;
      }
    }
    /* FIXME Some VLINK staff should be here */
  }
}

list *
calc_next_hop(struct top_hash_entry *par, slab *sl, slab *sll)
{
  struct spf_n *nh;
  list *l;

  if(par->nh==NULL)
  {
    if(par->lsa.type!=LSA_T_RT) return NULL;
    l=sl_alloc(sll);
    init_list(l);
    nh=sl_alloc(sl);
    nh->en=par;
    add_head(l, NODE nh);
    return l;
  }
  return par->nh;
}

list *
multi_next_hop(struct top_hash_entry *par, struct top_hash_entry *en, slab *sl,
 slab *sll)
{
  struct spf_n *n1,*n2;
  list *l1,*l2;

  l1=calc_next_hop(par,sl,sll);
  if(l1==NULL) return en->nh;
  if(en->nh==NULL) return l1;

  l2=sl_alloc(sll);
  init_list(l2);
  WALK_LIST(n1, *l1)
  {
    n2=sl_alloc(sl);
    memcpy(n2,n1,sizeof(struct spf_n));
    add_tail(l2,NODE n2);
  }

  WALK_LIST(n1, *en->nh)
  {
    n2=sl_alloc(sl);
    memcpy(n2,n1,sizeof(struct spf_n));
    add_tail(l2,NODE n2);
  }
  return l2;
}

