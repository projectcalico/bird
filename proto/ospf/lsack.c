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
  struct ospf_lsreq_header *lsh;
  int length;
  u8 i;

  nrid=ntohl(ps->ospf_packet.routerid);

  myrid=p->cf->global->router_id;

  if((n=find_neigh(ifa, nrid))==NULL)
  {
    debug("%s: Received lsack from unknown neigbor! (%u)\n", p->name,
      nrid);
    return ;
  }
  /* FIXME Go on! */
}

void
add_ack_list(struct ospf_neighbor *n,struct ospf_lsa_header *lsa)
{
  /* FIXME Go on */
}

