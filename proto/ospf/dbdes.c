/*
 *	BIRD -- OSPF
 *
 *	(c) 1999-2000 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

void
htonlsah(struct ospf_lsa_header *h, struct ospf_lsa_header *n)
{
  n->age=htons(h->age);
  n->options=h->options;
  n->type=h->type;
  n->id=htonl(h->id);
  n->rt=htonl(h->rt);
  n->sn=htonl(h->sn);
  n->checksum=htons(h->checksum);
  n->length=htons(h->length);
};

void
ntohlsah(struct ospf_lsa_header *n, struct ospf_lsa_header *h)
{
  h->age=ntohs(n->age);
  h->options=n->options;
  h->type=n->type;
  h->id=ntohl(n->id);
  h->rt=ntohl(n->rt);
  h->sn=ntohl(n->sn);
  h->checksum=ntohs(n->checksum);
  h->length=ntohs(n->length);
};
	
void
ospf_dbdes_tx(struct ospf_neighbor *n)
{
  struct ospf_dbdes_packet *pkt;
  struct ospf_packet *op;
  struct ospf_iface *ifa;
  u16 length;
  struct proto *p;
  u16 i,j;
  u8 *aa,*bb;

  ifa=n->ifa;

  p=(struct proto *)(ifa->proto);

  switch(n->state)
  {
    case NEIGHBOR_EXSTART:		/* Send empty packets */
      n->myimms.bit.i=1;
      pkt=(struct ospf_dbdes_packet *)(ifa->ip_sk->tbuf);
      op=(struct ospf_packet *)pkt;
      fill_ospf_pkt_hdr(ifa, pkt, DBDES);
      pkt->iface_mtu=htons(ifa->iface->mtu);	/*FIXME NOT for VLINK! */
      pkt->options= ifa->options;
      pkt->imms=n->myimms;
      pkt->ddseq=htonl(n->dds);
      length=sizeof(struct ospf_dbdes_packet);
      op->length=htons(length);
      ospf_pkt_finalize(ifa, op);
      sk_send_to(ifa->ip_sk,length, n->ip, OSPF_PROTO);
      debug("%s: DB_DES (I) sent for %u.\n", p->name, n->rid);
      break;

    case NEIGHBOR_EXCHANGE:
      n->myimms.bit.i=0;
      if(((n->myimms.bit.ms) && (n->dds==n->ddr+1)) ||
         ((!(n->myimms.bit.ms)) && (n->dds==n->ddr)))
      {
        snode *sn;			/* Send next */
        struct ospf_lsa_header *lsa;

	pkt=n->ldbdes;
        op=(struct ospf_packet *)pkt;
	
        fill_ospf_pkt_hdr(ifa, pkt, DBDES);
        pkt->iface_mtu=htons(ifa->iface->mtu);
        pkt->options= ifa->options;
	pkt->ddseq=htonl(n->dds);

	j=i=(ifa->iface->mtu-sizeof(struct ospf_dbdes_packet))/
		sizeof(struct ospf_lsa_header);	/* Number of lsaheaders */
	lsa=(n->ldbdes+sizeof(struct ospf_dbdes_packet));

	if(n->myimms.bit.m)
	{
	  sn=s_get(&(n->dbsi));

	  DBG("Number of LSA: %d\n", j);
	  for(;i>0;i--)
	  {
	    struct top_hash_entry *en;
	  
	    en=(struct top_hash_entry *)sn;
	    htonlsah(&(en->lsa), lsa);
	    DBG("Working on: %d\n", i);
            debug("\t%04x %08x %08x %p\n", en->lsa.type, en->lsa.id,
              en->lsa.rt, en->lsa_body);

	    if(sn->next==NULL)
	    {
	      break;	/* Should set some flag? */
  	    }
	    sn=sn->next;
	    lsa++;
	  }
	  i--;

	  if(sn->next==NULL)
	  {
	    DBG("Number of LSA NOT sent: %d\n", i);
	    DBG("M bit unset.\n");
	    n->myimms.bit.m=0;	/* Unset more bit */
	  }

	  s_put(&(n->dbsi),sn);
	}

        pkt->imms.byte=n->myimms.byte;

	length=(j-i)*sizeof(struct ospf_lsa_header)+
		sizeof(struct ospf_dbdes_packet);
	op->length=htons(length);
	
        ospf_pkt_finalize(ifa, op);
      }

    case NEIGHBOR_LOADING:
    case NEIGHBOR_FULL:

      aa=ifa->ip_sk->tbuf;
      bb=n->ldbdes;
      op=n->ldbdes;
      length=ntohs(op->length);

      for(i=0; i<ifa->iface->mtu; i++)
      {
        *(aa+i)=*(bb+i);	/* Copy last sent packet again */
      }

      {
        u8 ii;
	u8 *jj=ifa->ip_sk->tbuf;

	for(ii=0;ii<length;ii+=4)
	{
	  DBG("Out dump: %d,%d,%d,%d\n", *(jj+ii), *(jj+ii+1), *(jj+ii+2), *(jj+ii+3));
	}
      }

      sk_send_to(ifa->ip_sk,length, n->ip, OSPF_PROTO);
      debug("%s: DB_DES sent for %u.\n", p->name, n->rid);
      if(n->myimms.bit.ms) tm_start(n->rxmt_timer,ifa->rxmtint);
      else
      {
        if((n->myimms.bit.m==0) && (n->imms.bit.m==0) &&
          (n->state==NEIGHBOR_EXCHANGE))
	{
          ospf_neigh_sm(n, INM_EXDONE);
	  if(n->myimms.bit.ms) tm_stop(n->rxmt_timer);
	  else tm_start(n->rxmt_timer,ifa->rxmtint);
	}
      }
      break;

    default:				/* Ignore it */
      die("Bug in dbdes sending\n");
      break;
  }
}

void
rxmt_timer_hook(timer *timer)
{
  struct ospf_iface *ifa;
  struct proto *p;
  struct ospf_neighbor *n;

  n=(struct ospf_neighbor *)timer->data;
  ifa=n->ifa;
  p=(struct proto *)(ifa->proto);
  debug("%s: RXMT timer fired on interface %s for neigh: %u.\n",
    p->name, ifa->iface->name, n->rid);
  if(n->state<NEIGHBOR_LOADING) ospf_dbdes_tx(n);
  else
  {
    tm_stop(n->rxmt_timer);
   	/* FIXME I should dealloc ldbdes */
  }
}

int
lsa_comp(struct ospf_lsa_header *l1, struct ospf_lsa_header *l2)
	/* 
	 *  1	l1 is newer
	 *  0	identical
	 * -1	l2 is newer
	 */
{
  if(l1->sn<l2->sn) return 1;
  if(l1->sn==l2->sn)
  {
    if(l1->checksum=!l2->checksum) return l1->checksum<l2->checksum ? -1 :1;
    if(l1->age==MAXAGE) return 1;
    if(l2->age==MAXAGE) return -1;
    if(abs(l1->age-l2->age)>MAXAGEDIFF) return l1->age<l2->age ? 1 : -1;
  }
  return 0;

}

void
ospf_dbdes_reqladd(struct ospf_dbdes_packet *ps, struct proto *p,
  struct ospf_neighbor *n)
{
  struct ospf_lsa_header *plsa,lsa;
  struct top_hash_entry *he,*sn;
  struct top_graph *gr;
  struct ospf_packet *op;
  int i,j;

  gr=n->ifa->oa->gr;
  op=(struct ospf_packet *)ps;

  plsa=(void *)(ps+1);

  j=(ntohs(op->length)-sizeof(struct ospf_dbdes_packet))/
    sizeof( struct ospf_lsa_header);

  for(i=0;i<j;i++)
  {
  ntohlsah(plsa+i, &lsa);
  /* FIXME Test Checksum */
  if(((he=ospf_hash_find(gr,lsa.id,lsa.rt,lsa.type))==NULL)||
    (lsa_comp(&lsa, &(he->lsa))==1))
  {
     sn=sl_alloc(gr->hash_slab);
     ntohlsah(plsa+i, &(sn->lsa));
     s_add_tail(&(n->lsrql), SNODE sn);
  }
  }
}

void
ospf_dbdes_rx(struct ospf_dbdes_packet *ps, struct proto *p,
  struct ospf_iface *ifa, u16 size)
{
  u32 nrid, myrid;
  struct ospf_neighbor *n;
  u8 i;

  nrid=ntohl(((struct ospf_packet *)ps)->routerid);

  myrid=p->cf->global->router_id;

  if((n=find_neigh(ifa, nrid))==NULL)
  {
    debug("%s: Received dbdes from unknown neigbor! (%u)\n", p->name,
      nrid);
    return ;
  }

  if(ifa->iface->mtu<size)
  {
    debug("%s: Received dbdes larger than MTU from (%u)!\n", p->name, nrid);
    return ;
  }

  switch(n->state)
  {
    case NEIGHBOR_DOWN:
    case NEIGHBOR_ATTEMPT:
    case NEIGHBOR_2WAY:
        debug("%s: Received dbdes from %u in bad state. (%u)\n", p->name, nrid);
        return;
      break;
    case NEIGHBOR_INIT:
        ospf_neigh_sm(n, INM_2WAYREC);
	if(n->state!=NEIGHBOR_EXSTART) return;
    case NEIGHBOR_EXSTART:
        if((ps->imms.bit.m && ps->imms.bit.ms && ps->imms.bit.i)
          && (n->rid > myrid) &&
          (size == sizeof(struct ospf_dbdes_packet)))
        {
          /* I'm slave! */
          n->dds=ntohl(ps->ddseq);
	  n->ddr=ntohl(ps->ddseq);
	  n->options=ps->options;
	  n->myimms.bit.ms=0;
	  n->imms.byte=ps->imms.byte;
          debug("%s: I'm slave to %u. \n", p->name, nrid);
	  ospf_neigh_sm(n, INM_NEGDONE);
	  tm_stop(n->rxmt_timer);
	  ospf_dbdes_tx(n);
	  break;
        }
        else
        {
          if(((ps->imms.bit.i==0) && (ps->imms.bit.ms==0)) &&
            (n->rid < myrid) && (n->dds == ntohl(ps->ddseq)))
          {
            /* I'm master! */
	    n->options=ps->options;
            n->ddr=ntohl(ps->ddseq)-1;
            n->imms.byte=ps->imms.byte;
            debug("%s: I'm master to %u. \n", p->name, nrid);
	    ospf_neigh_sm(n, INM_NEGDONE);
          }
	  else
          {
            debug("%s: Nothing happend to %u (imms=%u)", p->name, nrid,
              ps->imms.byte);
            break;
          }
        }
        //break;	/* I should probably continue processing packet */

    case NEIGHBOR_EXCHANGE:
	if((ps->imms.byte==n->imms.byte) && (ps->options=n->options) &&
	  (ntohl(ps->ddseq)==n->ddr))
        {
          /* Duplicate packet */
          debug("%s: Received duplicate dbdes from (%u)!\n", p->name, nrid);
	  if(n->imms.bit.ms==0)
	  {
            ospf_dbdes_tx(n);
	  }
          return;
        }

        n->ddr=ntohl(ps->ddseq);

	if(ps->imms.bit.ms!=n->imms.bit.ms) /* M/S bit differs */
        {
          DBG("SEQMIS-BIT-MS\n");
          ospf_neigh_sm(n, INM_SEQMIS);
	  break;
        }

	if(ps->imms.bit.i)	/* I bit is set */
        {
          DBG("SEQMIS-BIT-I\n");
          ospf_neigh_sm(n, INM_SEQMIS);
	  break;
        }

	n->imms.byte=ps->imms.byte;

	if(ps->options!=n->options)	/* Options differs */
        {
          DBG("SEQMIS-OPT\n");
          ospf_neigh_sm(n, INM_SEQMIS);
	  break;
        }

	if(n->myimms.bit.ms)
        {
          if(ntohl(ps->ddseq)!=n->dds)		/* MASTER */
	  {
            DBG("SEQMIS-MASTER\n");
	    ospf_neigh_sm(n, INM_SEQMIS);
	    break;
	  }
	  n->dds++;
          DBG("Incrementing dds\n");
	  ospf_dbdes_reqladd(ps,p,n);
	  if((n->myimms.bit.m==0) && (ps->imms.bit.m==0))
	  {
	    ospf_neigh_sm(n, INM_EXDONE);
	  }
	  else
	  {
	    ospf_dbdes_tx(n);
	  }

        }
	else
        {
          if(ntohl(ps->ddseq)!=(n->dds+1))	/* SLAVE */
	  {
            DBG("SEQMIS-SLAVE\n");
	    ospf_neigh_sm(n, INM_SEQMIS);
	    break;
	  }
	  n->ddr=ntohl(ps->ddseq);
	  n->dds=ntohl(ps->ddseq);
	  ospf_dbdes_reqladd(ps,p,n);
	  ospf_dbdes_tx(n);
        }

      break;
    case NEIGHBOR_LOADING:
    case NEIGHBOR_FULL:
	if((ps->imms.byte==n->imms.byte) && (ps->options=n->options) &&
	  (ps->ddseq==n->dds)) /* Only duplicate are accepted */
        {
          debug("%s: Received duplicate dbdes from (%u)!\n", p->name, nrid);
          return;
        }
	else
        {
	  ospf_neigh_sm(n, INM_SEQMIS);
        }
      break;
    defaut:
      die("%s: Received dbdes from %u in unknown state. (%u)\n", p->name, nrid);
      break;
   }
}

