/*
 *	BIRD -- OSPF
 *
 *	(c) 1999 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

char *ospf_ns[]={"down", "attempt", "init", "2way", "exstart", "exchange",
  "loading", "full"};

const char *ospf_inm[]={ "hello received", "neighbor start", "2-way received",
  "negotiation done", "exstart done", "bad ls request", "load done",
  "adjacency ok?", "sequence mismatch", "1-way received", "kill neighbor",
  "inactivity timer", "line down" };

void
neigh_chstate(struct ospf_neighbor *n, u8 state)
{
  struct ospf_iface *ifa;
  struct proto *p;
  u8 oldstate;

  oldstate=n->state;

  if(oldstate!=state)
  {
    ifa=n->ifa;
    n->state=state;
    if(state==2WAY && oldstate<2WAY) ospf_int_sm(n->ifa, ISM_NEICH);
    if(state<2WAY && oldstate>=2WAY) ospf_int_sm(n->ifa, ISM_NEICH);
    if(oldstate==NEIGHBOR_FULL)	/* Decrease number of adjacencies */
    {
      ifa->fadj--;
      n->state=state;
      originate_rt_lsa(ifa->oa,ifa->oa->po);
      originate_net_lsa(ifa,ifa->oa->po);
    }
    p=(struct proto *)(ifa->proto);
  
    debug("%s: Neighbor %I changes state from \"%s\" to \"%s\".\n",
      p->name, n->ip, ospf_ns[oldstate], ospf_ns[state]);
    if(state==NEIGHBOR_FULL)	/* Increase number of adjacencies */
    {
      ifa->fadj++;
      originate_rt_lsa(n->ifa->oa,n->ifa->oa->po);
      originate_net_lsa(ifa,ifa->oa->po);
    }
    if(oldstate>=NEIGHBOR_EXSTART && state<NEIGHBOR_EXSTART)
    {
      tm_stop(n->rxmt_timer);
      /* Stop RXMT timers */
    }
    if(state==NEIGHBOR_EXSTART)
    {
      if(n->adj==0)	/* First time adjacency */
      {
        n->dds=random_u32();
      }
      n->dds++;
      n->myimms.byte=0;
      n->myimms.bit.ms=1;
      n->myimms.bit.m=1;
      n->myimms.bit.i=1;
      tm_start(n->rxmt_timer,1);	/* Or some other number ? */
    }
    if(state<NEIGHBOR_EXCHANGE) tm_stop(n->lsrr_timer);
  }
}

struct ospf_neighbor *
electbdr(list nl)
{
  struct ospf_neighbor *neigh,*n1,*n2;

  n1=NULL;
  n2=NULL;
  WALK_LIST (neigh, nl)		/* First try those decl. themselves */
  {
    if(neigh->state>=NEIGHBOR_2WAY)	/* Higher than 2WAY */
      if(neigh->priority>0)		/* Eligible */
        if(neigh->rid!=neigh->dr)	/* And not declaring itself DR */
	{
	  if(neigh->rid==neigh->bdr)	/* Declaring BDR */
          {
            if(n1!=NULL)
            {
              if(neigh->priority>n1->priority) n1=neigh;
	      else if(neigh->priority==n1->priority)
	          if(neigh->rid>n1->rid) n1=neigh;
            }
	    else
            {
              n1=neigh;
            }
          }
	  else				/* And NOT declaring BDR */
          {
            if(n2!=NULL)
            {
              if(neigh->priority>n2->priority) n2=neigh;
	      else if(neigh->priority==n2->priority)
	          if(neigh->rid>n2->rid) n2=neigh;
            }
	    else
            {
              n2=neigh;
            }
          }
      }
  }
  if(n1==NULL) n1=n2;

  return(n1);
}

struct ospf_neighbor *
electdr(list nl)
{
  struct ospf_neighbor *neigh,*n;

  n=NULL;
  WALK_LIST (neigh, nl)	/* And now DR */
  {
    if(neigh->state>=NEIGHBOR_2WAY)	/* Higher than 2WAY */
      if(neigh->priority>0)		/* Eligible */
        if(neigh->rid==neigh->dr)	/* And declaring itself DR */
	{
          if(n!=NULL)
          {
            if(neigh->priority>n->priority) n=neigh;
	    else if(neigh->priority==n->priority)
	        if(neigh->rid>n->rid) n=neigh;
          }
	  else
          {
            n=neigh;
          }
      }
  }

  return(n);
}

int
can_do_adj(struct ospf_neighbor *n)
{
  struct ospf_iface *ifa;
  struct proto *p;
  int i;

  ifa=n->ifa;
  p=(struct proto *)(ifa->proto);
  i=0;

  switch(ifa->type)
  {
    case OSPF_IT_PTP:
    case OSPF_IT_VLINK:
      i=1;
      break;
    case OSPF_IT_BCAST:
    case OSPF_IT_NBMA:
      switch(ifa->state)
      {
        case OSPF_IS_DOWN:
          die("%s: Iface %s in down state?", p->name, ifa->iface->name);
          break;
        case OSPF_IS_WAITING:
          DBG("%s: Neighbor? on iface %s\n",p->name, ifa->iface->name);
          break;
        case OSPF_IS_DROTHER:
          if(((n->rid==ifa->drid) || (n->rid==ifa->bdrid))
            && (n->state==NEIGHBOR_2WAY)) i=1;
          break;
        case OSPF_IS_PTP:
        case OSPF_IS_BACKUP:
        case OSPF_IS_DR:
          if(n->state==NEIGHBOR_2WAY) i=1;
          break;
        default:
          die("%s: Iface %s in unknown state?",p->name, ifa->iface->name);
          break;
      }
      break;
    default:
      die("%s: Iface %s is unknown type?",p->name, ifa->iface->name);
      break;
  }
  DBG("%s: Iface %s can_do_adj=%d\n",p->name, ifa->iface->name,i);
  return i;
}

void
ospf_neigh_sm(struct ospf_neighbor *n, int event)
	/* Interface state machine */
{
  struct proto *p=(struct proto *)(n->ifa->proto);
  struct proto_ospf *po=n->ifa->proto;

  DBG("%s: Neighbor state machine for neighbor %I, event \"%s\".\n",
    p->name, n->rid, ospf_inm[event]);

  switch(event)
  {
    case INM_START:
      neigh_chstate(n,NEIGHBOR_ATTEMPT);
      /* FIXME No NBMA now */
      break;
    case INM_HELLOREC:
      switch(n->state)
      {
        case NEIGHBOR_ATTEMPT:
	case NEIGHBOR_DOWN:
	  neigh_chstate(n,NEIGHBOR_INIT);
	default:
          restart_inactim(n);
	  break;
      }
      break;
    case INM_2WAYREC:
      if(n->state<NEIGHBOR_2WAY)
      {
        /* Can In build adjacency? */
        neigh_chstate(n,NEIGHBOR_2WAY);
	if(can_do_adj(n))
        {
          neigh_chstate(n,NEIGHBOR_EXSTART);
        }
      }
      break;
    case INM_NEGDONE:
      if(n->state==NEIGHBOR_EXSTART)
      {
        neigh_chstate(n,NEIGHBOR_EXCHANGE);
        s_init_list(&(n->lsrql));
	n->lsrqh=ospf_top_new(n->ifa->proto);
        s_init_list(&(n->lsrtl));
	n->lsrth=ospf_top_new(n->ifa->proto);
	s_init(&(n->dbsi), &(n->ifa->oa->lsal));
	s_init(&(n->lsrqi), &(n->lsrql));
	s_init(&(n->lsrti), &(n->lsrtl));
	tm_start(n->lsrr_timer,n->ifa->rxmtint);
	tm_start(n->ackd_timer,n->ifa->rxmtint/2);
      }
      else die("NEGDONE and I'm not in EXSTART?\n");
      break;
    case INM_EXDONE:
        neigh_chstate(n,NEIGHBOR_LOADING);
      break;
    case INM_LOADDONE:
        neigh_chstate(n,NEIGHBOR_FULL);
      break;
    case INM_ADJOK:
        switch(n->state)
        {
          case NEIGHBOR_2WAY:
        /* Can In build adjacency? */
            if(can_do_adj(n))
            {
              neigh_chstate(n,NEIGHBOR_EXSTART);
            }
            break;
          default:
	    if(n->state>=NEIGHBOR_EXSTART)
              if(!can_do_adj(n))
              {
                neigh_chstate(n,NEIGHBOR_2WAY);
              }
            break;
        }
      break;
    case INM_SEQMIS:
    case INM_BADLSREQ:
      debug("%s: Bad LS req!\n", p->name);
      if(n->state>=NEIGHBOR_EXCHANGE)
      {
        neigh_chstate(n,NEIGHBOR_EXSTART);
      }
      break;
    case INM_KILLNBR:
    case INM_LLDOWN:
    case INM_INACTTIM:
      neigh_chstate(n,NEIGHBOR_DOWN);
      break;
    case INM_1WAYREC:
      neigh_chstate(n,NEIGHBOR_INIT);
      break;
    default:
      die("%s: INM - Unknown event?",p->name);
      break;
  }
}

void
bdr_election(struct ospf_iface *ifa, struct proto *p)
{
  struct ospf_neighbor *neigh,*ndr,*nbdr,me,*tmp;
  u32 myid, ndrid, nbdrid;
  int doadj;

  p=(struct proto *)(ifa->proto);

  DBG("%s: (B)DR election.\n",p->name);

  myid=p->cf->global->router_id;

  me.state=NEIGHBOR_2WAY;
  me.rid=myid;
  me.priority=ifa->priority;
  me.dr=ifa->drid;
  me.bdr=ifa->bdrid;

  add_tail(&ifa->neigh_list, NODE &me);

  nbdr=electbdr(ifa->neigh_list);
  ndr=electdr(ifa->neigh_list);

  if(ndr==NULL) ndr=nbdr;

  if(((ifa->drid==myid) && (ndr->rid!=myid))
    || ((ifa->drid!=myid) && (ndr->rid==myid))
    || ((ifa->bdrid==myid) && (nbdr->rid!=myid)) 
    || ((ifa->bdrid!=myid) && (nbdr->rid==myid)))
  {
    if(ndr==NULL) ifa->drid=me.dr=0;
    else ifa->drid=me.dr=ndr->rid;

    if(nbdr==NULL) ifa->bdrid=me.bdr=0;
    else ifa->bdrid=me.bdr=nbdr->rid;

    nbdr=electbdr(ifa->neigh_list);
    ndr=electdr(ifa->neigh_list);
  }

  if(ndr==NULL) ifa->drid=0;
  if(ndr==NULL) ndrid=0;
  else ndrid=ndr->rid;

  if(nbdr==NULL) nbdrid=0;
  else nbdrid=nbdr->rid;

  doadj=0;
  if((ifa->drid!=ndrid) || (ifa->bdrid!=nbdrid)) doadj=1;
  ifa->drid=ndrid;
  if((tmp=find_neigh(ifa,ndrid))==NULL) die("Error i BDR election.\n");
  ifa->drip=tmp->ip;
  ifa->bdrid=nbdrid;
  if((tmp=find_neigh(ifa,nbdrid))==NULL) die("Error i BDR election.\n");
  ifa->bdrip=tmp->ip;

  DBG("%s: DR=%I, BDR=%I\n",p->name, ifa->drid, ifa->bdrid);

  if(myid==ifa->drid) iface_chstate(ifa, OSPF_IS_DR);
  else
  {
    if(myid==ifa->bdrid) iface_chstate(ifa, OSPF_IS_BACKUP);
    else iface_chstate(ifa, OSPF_IS_DROTHER);
  }

  rem_node(NODE &me);

  if(doadj)
  {
    WALK_LIST (neigh, ifa->neigh_list)
    {
      ospf_neigh_sm(neigh, INM_ADJOK);
    }
  }
}

struct ospf_neighbor *
find_neigh(struct ospf_iface *ifa, u32 rid)
{
  struct ospf_neighbor *n;

  WALK_LIST (n, ifa->neigh_list)
    if(n->rid == rid)
      return n;
  return NULL;
}

struct ospf_neighbor *
find_neigh_noifa(struct proto_ospf *po, u32 rid)
{
  struct ospf_neighbor *n;
  struct ospf_iface *ifa;

  WALK_LIST (ifa, po->iface_list)
    if((n=find_neigh(ifa, rid))!=NULL)
      return n;
  return NULL;
}

struct ospf_area *
ospf_find_area(struct proto_ospf *po, u32 aid)
{
  struct ospf_area *oa;
  WALK_LIST(NODE oa,po->area_list)
    if(((struct ospf_area *)oa)->areaid==aid) return oa;
  return NULL;
}


