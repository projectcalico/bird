/*
 *	BIRD -- OSPF
 *
 *	(c) 1999 - 2000 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

char *ospf_ns[]={"    down",
                 " attempt",
                 "    init",
                 "    2way",
                 " exstart",
                 "exchange",
                 " loading",
                 "    full"};

const char *ospf_inm[]={ "hello received", "neighbor start", "2-way received",
  "negotiation done", "exstart done", "bad ls request", "load done",
  "adjacency ok?", "sequence mismatch", "1-way received", "kill neighbor",
  "inactivity timer", "line down" };

/**
 * neigh_chstate - handles changes related to new or lod state of neighbor
 * @n: OSPF neighbor
 * @state: new state
 *
 * Many actions has to be taken acording to state change of neighbor. It
 * starts rxmt timers, call interface state machine etc.
 */

void
neigh_chstate(struct ospf_neighbor *n, u8 state)
{
  u8 oldstate;

  oldstate=n->state;

  if(oldstate!=state)
  {
    struct ospf_iface *ifa=n->ifa;
    struct proto_ospf *po=ifa->oa->po;
    struct proto *p=&po->proto;

    n->state=state;

    OSPF_TRACE( D_EVENTS, "Neighbor %I changes state from \"%s\" to \"%s\".",
      n->ip, ospf_ns[oldstate], ospf_ns[state]);

    if((state==NEIGHBOR_2WAY) && (oldstate<NEIGHBOR_2WAY))
      ospf_int_sm(ifa, ISM_NEICH);
    if((state<NEIGHBOR_2WAY) && (oldstate>=NEIGHBOR_2WAY))
      ospf_int_sm(ifa, ISM_NEICH);

    if(oldstate==NEIGHBOR_FULL)	/* Decrease number of adjacencies */
    {
      ifa->fadj--;
      schedule_rt_lsa(ifa->oa);
      originate_net_lsa(ifa);
    }
  
    if(state==NEIGHBOR_FULL)	/* Increase number of adjacencies */
    {
      ifa->fadj++;
      schedule_rt_lsa(ifa->oa);
      originate_net_lsa(ifa);
    }
    if(oldstate>=NEIGHBOR_EXSTART && state<NEIGHBOR_EXSTART)
    {
      /* Stop RXMT timer */
      tm_stop(n->rxmt_timer);
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
        if(ipa_compare(neigh->ip,neigh->dr)!=0)	/* And not decl. itself DR */
	{
	  if(ipa_compare(neigh->ip,neigh->bdr)==0)	/* Declaring BDR */
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
        if(ipa_compare(neigh->ip,neigh->dr)==0)	/* And declaring itself DR */
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
          bug("%s: Iface %s in down state?", p->name, ifa->iface->name);
          break;
        case OSPF_IS_WAITING:
          DBG("%s: Neighbor? on iface %s\n",p->name, ifa->iface->name);
          break;
        case OSPF_IS_DROTHER:
          if(((n->rid==ifa->drid) || (n->rid==ifa->bdrid))
            && (n->state>=NEIGHBOR_2WAY)) i=1;
          break;
        case OSPF_IS_PTP:
        case OSPF_IS_BACKUP:
        case OSPF_IS_DR:
          if(n->state>=NEIGHBOR_2WAY) i=1;
          break;
        default:
          bug("%s: Iface %s in unknown state?",p->name, ifa->iface->name);
          break;
      }
      break;
    default:
      bug("%s: Iface %s is unknown type?",p->name, ifa->iface->name);
      break;
  }
  DBG("%s: Iface %s can_do_adj=%d\n",p->name, ifa->iface->name,i);
  return i;
}

/**
 * ospf_neigh_sm - ospf neighbor state machine
 * @n: neighor
 * @event: actual event
 *
 * This part implements neighbor state machine as described in 10.3 of
 * RFC 2328. the only difference is that state %NEIGHBOR_ATTEMPT is not
 * used. We discover neighbors on nonbroadcast networks using the
 * same ways as on broadcast networks. The only difference is in
 * sending hello packets. These are send to IPs listed in
 * @ospf_iface->nbma_list .
 */
void
ospf_neigh_sm(struct ospf_neighbor *n, int event)
{
  struct proto *p=(struct proto *)(n->ifa->proto);
  struct proto_ospf *po=n->ifa->proto;

  DBG("%s: Neighbor state machine for neighbor %I, event \"%s\".\n",
    p->name, n->rid, ospf_inm[event]);

  switch(event)
  {
    case INM_START:
      neigh_chstate(n,NEIGHBOR_ATTEMPT);
      /* NBMA are used different way */
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
      if(n->state<NEIGHBOR_2WAY) neigh_chstate(n,NEIGHBOR_2WAY);
      if((n->state==NEIGHBOR_2WAY) && can_do_adj(n))
        neigh_chstate(n,NEIGHBOR_EXSTART);
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
      else bug("NEGDONE and I'm not in EXSTART?");
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
      OSPF_TRACE(D_EVENTS, "Bad LS req!");
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
      bug("%s: INM - Unknown event?",p->name);
      break;
  }
}

/**
 * bdr_election - (Backup) Designed Router election
 * @ifa: actual interface
 *
 * When wait time fires, it time to elect (Backup) Designed Router.
 * Structure describing me is added to this list so every electing router
 * has the same list. Backup Designed Router is elected before Designed
 * Router. This process is described in 9.4 of RFC 2328.
 */
void
bdr_election(struct ospf_iface *ifa)
{
  struct ospf_neighbor *neigh,*ndr,*nbdr,me,*tmp;
  u32 myid;
  ip_addr ndrip, nbdrip;
  int doadj;
  struct proto *p=&ifa->proto->proto;

  DBG("(B)DR election.\n");

  myid=p->cf->global->router_id;

  me.state=NEIGHBOR_2WAY;
  me.rid=myid;
  me.priority=ifa->priority;
  me.dr=ifa->drip;
  me.bdr=ifa->bdrip;
  me.ip=ifa->iface->addr->ip;

  add_tail(&ifa->neigh_list, NODE &me);

  nbdr=electbdr(ifa->neigh_list);
  ndr=electdr(ifa->neigh_list);

  if(ndr==NULL) ndr=nbdr;

  if(((ifa->drid==myid) && (ndr!=&me))
    || ((ifa->drid!=myid) && (ndr==&me))
    || ((ifa->bdrid==myid) && (nbdr!=&me)) 
    || ((ifa->bdrid!=myid) && (nbdr==&me)))
  {
    if(ndr==NULL) ifa->drip=me.dr=ipa_from_u32(0);
    else ifa->drip=me.dr=ndr->ip;

    if(nbdr==NULL) ifa->bdrip=me.bdr=ipa_from_u32(0);
    else ifa->bdrip=me.bdr=nbdr->ip;

    nbdr=electbdr(ifa->neigh_list);
    ndr=electdr(ifa->neigh_list);
  }

  if(ndr==NULL) ndrip=ipa_from_u32(0);
  else ndrip=ndr->ip;

  if(nbdr==NULL) nbdrip=ipa_from_u32(0);
  else nbdrip=nbdr->ip;

  doadj=0;
  if((ipa_compare(ifa->drip,ndrip)!=0) || (ipa_compare(ifa->bdrip,nbdrip)!=0))
    doadj=1;

  if(ndr==NULL)
  {
    ifa->drid=0;
    ifa->drip=ipa_from_u32(0);
  }
  else
  {
    ifa->drid=ndr->rid;
    ifa->drip=ndr->ip;
  }

  if(nbdr==NULL)
  {
    ifa->bdrid=0;
    ifa->bdrip=ipa_from_u32(0);
  }
  else
  {
    ifa->bdrid=nbdr->rid;
    ifa->bdrip=nbdr->ip;
  }

  DBG("DR=%I, BDR=%I\n", ifa->drid, ifa->bdrid);

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

/* Neighbor is inactive for a long time. Remove it. */
void
neighbor_timer_hook(timer *timer)
{
  struct ospf_neighbor *n;
  struct ospf_iface *ifa;
  struct proto *p;

  n=(struct ospf_neighbor *)timer->data;
  ifa=n->ifa;
  p=(struct proto *)(ifa->proto);
  OSPF_TRACE(D_EVENTS,"Inactivity timer fired on interface %s for neighbor %I.",
    ifa->iface->name, n->ip);
  ospf_neigh_remove(n);
}

void
ospf_neigh_remove(struct ospf_neighbor *n)
{
  struct ospf_iface *ifa;
  struct proto *p;

  ifa=n->ifa;
  p=(struct proto *)(ifa->proto);
  neigh_chstate(n, NEIGHBOR_DOWN);
  tm_stop(n->inactim);
  rfree(n->inactim);
  if(n->rxmt_timer!=NULL)
  {
    tm_stop(n->rxmt_timer);
    rfree(n->rxmt_timer);
  }
  if(n->lsrr_timer!=NULL)
  {
    tm_stop(n->lsrr_timer);
    rfree(n->lsrr_timer);
  }
  if(n->ackd_timer!=NULL)
  {
    tm_stop(n->ackd_timer);
    rfree(n->ackd_timer);
  }
  if(n->ldbdes!=NULL)
  {
    mb_free(n->ldbdes);
  }
  if(n->lsrqh!=NULL)
  {
    ospf_top_free(n->lsrqh);
  }
  if(n->lsrth!=NULL)
  {
    ospf_top_free(n->lsrth);
  }
  rem_node(NODE n);
  mb_free(n);
  OSPF_TRACE(D_EVENTS, "Deleting neigbor.");
}

void
ospf_sh_neigh_info(struct ospf_neighbor *n)
{
   struct ospf_iface *ifa=n->ifa;
   char *pos="other";
   char etime[6];
   int exp,sec,min;

   exp=n->inactim->expires-now;
   sec=exp-(exp/60);
   min=(exp-sec)/60;
   if(min>59)
   {
     bsprintf(etime,"-Inf-");
   }
   else
   {
     bsprintf(etime,"%02u:%02u", min, sec);
   }
   
   if(n->rid==ifa->drid) pos="dr   ";
   if(n->rid==ifa->bdrid) pos="bdr  ";
   if(n->ifa->type==OSPF_IT_PTP) pos="ptp  ";

   cli_msg(-1013,"%-18I\t%3u\t%s/%s\t%-5s\t%-18I\t%-10s",n->rid, n->priority,
     ospf_ns[n->state], pos, etime, n->ip,ifa->iface->name);
}
