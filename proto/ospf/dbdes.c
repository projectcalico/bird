/*
 *	BIRD -- OSPF
 *
 *	(c) 1999 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

void
ospf_dbdes_tx(struct ospf_neighbor *n)
{
  struct ospf_dbdes_packet *pkt;
  struct ospf_packet *op;
  struct ospf_iface *ifa;
  u16 length;
  struct proto *p;

  ifa=n->ifa;

  p=(struct proto *)(ifa->proto);

  switch(n->state)
  {
    case NEIGHBOR_EXSTART:		/* Send empty packets */
      pkt=(struct ospf_dbdes_packet *)(ifa->ip_sk->tbuf);
      op=(struct ospf_packet *)pkt;

      fill_ospf_pkt_hdr(ifa, pkt, DBDES);
      pkt->iface_mtu= ((struct iface *)ifa)->mtu;
      pkt->options= ifa->options;
      pkt->imms=n->myimms;
      pkt->ddseq=n->dds;
      length=sizeof(struct ospf_dbdes_packet);
      op->length=htons(length);
      ospf_pkt_finalize(ifa, op);
      sk_send_to(ifa->ip_sk,length, n->ip, OSPF_PROTO);
      debug("%s: DB_DES sent for %u.\n", p->name, n->rid);

    /*case NEIGHBOR_EXCHANGE:		*/
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
        if(ps->imms==(DBDES_I|DBDES_M|DBDES_MS) && (n->rid > myrid) &&
          (size == sizeof(struct ospf_dbdes_packet)))
        {
          /* I'm slave! */
          n->dds=ps->ddseq;
	  n->options=ps->options;
	  n->myimms=(n->myimms && DBDES_M);
	  n->ddr=ps->ddseq;
	  n->imms=ps->imms;
          debug("%s: I'm slave to %u. \n", p->name, nrid);
	  ospf_neigh_sm(n, INM_NEGDONE);
        }
        else
        {
          if(((ps->imms & (DBDES_I|DBDES_MS))== 0) && (n->rid < myrid) &&
            (n->dds == ps->ddseq))
          {
            /* I'm master! */
	    n->options=ps->options;
            n->ddr=ps->ddseq;
            n->imms=ps->imms;
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
	if((ps->imms==n->imms) && (ps->options=n->options) &&
	  (ps->ddseq==n->dds))
        {
          /* Duplicate packet */
          debug("%s: Received duplicate dbdes from (%u)!\n", p->name, nrid);
	  if(!IAMMASTER(n->imms))
	  {
            ospf_dbdes_tx(n);
	  }
          return;
        }

	if(IAMMASTER(ps->imms)!=IAMMASTER(n->myimms)) /* M/S bit differs */
        {
          ospf_neigh_sm(n, INM_SEQMIS);
	  break;
        }

	if(INISET(ps->imms))	/* I bit is set */
        {
          ospf_neigh_sm(n, INM_SEQMIS);
	  break;
        }

	if(ps->options!=n->options)	/* Options differs */
        {
          ospf_neigh_sm(n, INM_SEQMIS);
	  break;
        }

	if(IAMMASTER(n->myimms))
        {
          if(ps->ddseq!=n->dds)
	  {
	    ospf_neigh_sm(n, INM_SEQMIS);
	    break;
	  }
        }
	else
        {
          if(ps->ddseq!=(n->dds+1))
	  {
	    ospf_neigh_sm(n, INM_SEQMIS);
	    break;
	  }
        }

	/* FIXME: Packet accepted, go on */

      break;
    case NEIGHBOR_LOADING:
    case NEIGHBOR_FULL:
	if((ps->imms==n->imms) && (ps->options=n->options) &&
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

