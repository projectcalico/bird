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
  struct top_hash_entry *en;
  slab *sl;
  struct spf_n *cn;
  u32 i,*rts;

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

  cn=sl_alloc(sl);
  cn->en=oa->rt;
  oa->rt->dist=0;
  oa->rt->color=CANDIDATE;
  add_head(&oa->cand,NODE cn);

  while(!EMPTY_LIST(oa->cand))
  {
    struct top_hash_entry *act,*tmp;
    node *n;
    struct ospf_lsa_rt *rt;
    struct ospf_lsa_rt_link *rtl;
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
	  add_cand(&oa->cand,tmp,act->dist+rtl->metric,sl);
	}
        break;
      case LSA_T_NET:
	net=(struct ospf_lsa_net *)act->lsa_body;
	rts=(u32 *)(net+1);
	for(i=0;i<(act->lsa.length-sizeof(struct ospf_lsa_header)-
	  sizeof(struct ospf_lsa_net))/sizeof(u32);i++)
	{
	  tmp=ospf_hash_find(oa->gr, *rts, *rts, LSA_T_RT);
          add_cand(&oa->cand,tmp,act->dist,sl);
	}
        break;
    }
  }
}

void
add_cand(list *l, struct top_hash_entry *en, u16 dist, slab *s)
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
    //Next hop calc
  }
  else
  {
    /* Clear next hop */

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
  }
}
