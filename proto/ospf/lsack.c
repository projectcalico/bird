/*
 *	BIRD -- OSPF
 *
 *	(c) 2000 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

void
ospf_lsack_tx(struct ospf_neighbor *n)
{
  /* FIXME Go on! */
}

void
ospf_lsack_rx(struct ospf_lsack_packet *ps, struct proto *p,
  struct ospf_iface *ifa, u16 size)
{
  u32 nrid, myrid;
  struct ospf_neighbor *n;
  struct ospf_lsa_header lsa,*plsa;
  int length;
  u16 nolsa,i;
  struct top_hash_entry *en;

  nrid=ntohl(ps->ospf_packet.routerid);

  myrid=p->cf->global->router_id;

  if((n=find_neigh(ifa, nrid))==NULL)
  {
    debug("%s: Received lsack from unknown neigbor! (%u)\n", p->name,
      nrid);
    return ;
  }
  
  nolsa=(ntohs(ps->ospf_packet.length)-sizeof(struct ospf_lsack_packet))/
    sizeof(struct ospf_lsa_header);
  DBG("Received %d lsa\n",nolsa);
  plsa=( struct ospf_lsa_header *)(ps+1);

  for(i=0;i<nolsa;i++)
  {
    ntohlsah(plsa+i,&lsa);
    if((en=ospf_hash_find_header(n->lsrth,&lsa))==NULL) continue;

    if(lsa_comp(&lsa,&en->lsa)!=CMP_SAME)
    {
      log("Strange LS acknoledgement from %d\n",n->rid);
      continue;
    }

    DBG("Deleting LS Id: %u RT: % Type: %u from LS Retl for neighbor %u\n",
      lsa.id,lsa.rt,lsa.type,n->rid);
    s_rem_node(SNODE en);
    ospf_hash_delete(n->lsrth,en);
  }  
}

void
add_ack_list(struct ospf_neighbor *n,struct ospf_lsa_header *lsa)
{
  /* FIXME Go on */
}

