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
      pkt->iface_mtu=htons(ifa->iface->mtu);
      pkt->options= ifa->options;
      pkt->imms=n->myimms;
      pkt->ddseq=n->dds;
      length=sizeof(struct ospf_dbdes_packet);
      op->length=htons(length);
      ospf_pkt_finalize(ifa, op);
      sk_send_to(ifa->ip_sk,length, n->ip, OSPF_PROTO);
      debug("%s: DB_DES (I) sent for %u.\n", p->name, n->rid);
      break;

    case NEIGHBOR_EXCHANGE:
      n->myimms.bit.i=0;
      if(! (((n->myimms.bit.ms) && (n->dds==n->ddr+1)) ||
         ((!(n->myimms.bit.ms)) && (n->dds==n->ddr))))
      {
        snode *sn;			/* Send next */
        struct ospf_lsa_header *lsa;

	pkt=n->ldbdes;
        op=(struct ospf_packet *)pkt;
	
        fill_ospf_pkt_hdr(ifa, pkt, DBDES);
        pkt->iface_mtu=htons(ifa->iface->mtu);
        pkt->options= ifa->options;
	pkt->ddseq=n->dds;

	j=i=(ifa->iface->mtu-sizeof(struct ospf_dbdes_packet))/
		sizeof(struct ospf_lsa_header);	/* Number of lsaheaders */
	lsa=(n->ldbdes+sizeof(struct ospf_dbdes_packet));

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
	  DBG("Ini: %d, M: %d MS: %d.\n",n->imms.bit.i, n->imms.bit.m, n->imms.bit.ms);
	}

	s_put(&(n->dbsi),sn);

        pkt->imms.byte=n->myimms.byte;

	length=(j-i)*sizeof(struct ospf_lsa_header)+
		sizeof(struct ospf_dbdes_packet);
	op->length=htons(length);
	
        ospf_pkt_finalize(ifa, op);
      }

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

    default:				/* Ignore it */
      break;
  }
}

void
rxmt_timer_hook(timer *timer)
{
  struct ospf_iface *ifa;
  struct proto *p;
  struct ospf_neighbor *n;

  ifa=(struct ospf_iface *)timer->data;
  p=(struct proto *)(ifa->proto);
  debug("%s: RXMT timer fired on interface %s.\n",
    p->name, ifa->iface->name);
  WALK_LIST (n, ifa->neigh_list)	/* Try to send db_des */
  {
    ospf_dbdes_tx(n);
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
          n->dds=ps->ddseq;
	  n->options=ps->options;
	  n->myimms.bit.ms=0;
	  n->ddr=ps->ddseq;
	  n->imms.byte=ps->imms.byte;
          debug("%s: I'm slave to %u. \n", p->name, nrid);
	  ospf_neigh_sm(n, INM_NEGDONE);
        }
        else
        {
          if(((ps->imms.bit.i==0) && (ps->imms.bit.ms==0)) &&
            (n->rid < myrid) && (n->dds == ps->ddseq))
          {
            /* I'm master! */
	    n->options=ps->options;
            n->ddr=ps->ddseq;
            n->imms.byte=ps->imms.byte;
            debug("%s: I'm master to %u. \n", p->name, nrid);
	    ospf_neigh_sm(n, INM_NEGDONE);
          }
	  else
          {
            debug("%s: Nothing happend to %u (imms=%u)", p->name, nrid,
              ps->imms);
            break;
          }
        }
        break;	/* I should probably continue processing packet */

    case NEIGHBOR_EXCHANGE:
	if((ps->imms.byte==n->imms.byte) && (ps->options=n->options) &&
	  (ps->ddseq==n->dds))
        {
          /* Duplicate packet */
          debug("%s: Received duplicate dbdes from (%u)!\n", p->name, nrid);
	  if(n->imms.bit.ms==0)
	  {
            ospf_dbdes_tx(n);
	  }
          return;
        }

	if(ps->imms.bit.ms!=n->myimms.bit.m) /* M/S bit differs */
        {
          DBG("SEQMIS-IMMS\n");
          ospf_neigh_sm(n, INM_SEQMIS);
	  break;
        }

	if(ps->imms.bit.i)	/* I bit is set */
        {
          DBG("SEQMIS-BIT-I\n");
          ospf_neigh_sm(n, INM_SEQMIS);
	  break;
        }

	if(ps->options!=n->options)	/* Options differs */
        {
          DBG("SEQMIS-OPT\n");
          ospf_neigh_sm(n, INM_SEQMIS);
	  break;
        }

	if(n->myimms.bit.ms)
        {
          if(ps->ddseq!=n->dds)
	  {
            DBG("SEQMIS-MASTER\n");
	    ospf_neigh_sm(n, INM_SEQMIS);
	    break;
	  }
        }
	else
        {
          if(ps->ddseq!=(n->dds+1))
	  {
            DBG("SEQMIS-SLAVE\n");
	    ospf_neigh_sm(n, INM_SEQMIS);
	    break;
	  }
        }

	/* FIXME: Packet accepted, go on */

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

