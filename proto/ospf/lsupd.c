/*
 *	BIRD -- OSPF
 *
 *	(c) 2000 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

void
ospf_lsupd_tx(struct ospf_neighbor *n)
{
  /* FIXME Go on! */
}

void		/* I send all I received in LSREQ */
ospf_lsupd_tx_list(struct ospf_neighbor *n, list *l)
{
  struct l_lsr_head *llsh;
  u16 len;
  u32 lsano;
  struct top_hash_entry *en;
  struct ospf_lsupd_packet *pk;
  struct ospf_packet *op;
  void *pktpos;
  u8 ii;
  u8 *jj=n->ifa->ip_sk->tbuf;

  if(HEAD(*l)==NULL) return;

  pk=(struct ospf_lsupd_packet *)n->ifa->ip_sk->tbuf;
  op=(struct ospf_packet *)n->ifa->ip_sk->tbuf;

  DBG("LSupd: 1st packet\n");
       
  fill_ospf_pkt_hdr(n->ifa, pk, LSUPD);
  len=SIPH+sizeof(struct ospf_lsupd_packet);
  lsano=0;
  pktpos=(pk+1);

  WALK_LIST(llsh, *l)
  {
    en=ospf_hash_find(n->ifa->oa->gr,llsh->lsh.id,llsh->lsh.rt,llsh->lsh.type);
    DBG("Sending ID=%u, Type=%u, RT=%u\n", llsh->lsh.id, llsh->lsh.type,
      llsh->lsh.rt);
    if((len+sizeof(struct ospf_lsa_header)+en->lsa.length)>n->ifa->iface->mtu)
    {
      pk->lsano=htonl(lsano);
      op->length=htons(len);
      ospf_pkt_finalize(n->ifa, op);
		       
      for(ii=0;ii<(len-SIPH);ii+=4)
        DBG("Out dump: %d,%d,%d,%d\n", *(jj+ii), *(jj+ii+1), *(jj+ii+2), *(jj+ii+3));

      sk_send_to(n->ifa->ip_sk,len, n->ip, OSPF_PROTO);

      DBG("LSupd: next packet\n");
      fill_ospf_pkt_hdr(n->ifa, pk, LSUPD);
      len=SIPH+sizeof(struct ospf_lsupd_packet);
      lsano=0;
      pktpos=(pk+1);
    }
    htonlsah(&(en->lsa), pktpos);
    pktpos=pktpos+sizeof(struct ospf_lsa_header);
    htonlsab(en->lsa_body, pktpos, en->lsa.type, en->lsa.length);
    pktpos=pktpos+en->lsa.length-sizeof(struct ospf_lsa_header);
    len=len+en->lsa.length;
    lsano++;
  }
  pk->lsano=htonl(lsano);
  op->length=htons(len-SIPH);
  ospf_pkt_finalize(n->ifa, op);

  for(ii=0;ii<(len-SIPH);ii+=4)
    DBG("Out dump: %d,%d,%d,%d\n", *(jj+ii), *(jj+ii+1), *(jj+ii+2), *(jj+ii+3));

  sk_send_to(n->ifa->ip_sk,len, n->ip, OSPF_PROTO);
  DBG("LSupd: sent\n");
}

void
ospf_lsupd_rx(struct ospf_lsupd_packet *ps, struct proto *p,
  struct ospf_iface *ifa, u16 size)
{
  u32 area,nrid,myrid;
  struct ospf_neighbor *n;
  struct ospf_lsa_header *lsa;
  struct ospf_area *oa;
  struct proto_ospf *po=(struct proto_ospf *)p;
  u16 length;
  u8 i;

  nrid=ntohl(ps->ospf_packet.routerid);

  myrid=p->cf->global->router_id;

  if((n=find_neigh(ifa, nrid))==NULL)
  {
    debug("%s: Received lsupd from unknown neigbor! (%u)\n", p->name,
      nrid);
    return ;
  }
  if(n->state<NEIGHBOR_EXCHANGE)
  {
    debug("%s: Received lsupd in lesser state than EXCHANGE from (%u)\n",
      p->name);
    return;
  }

  lsa=(struct ospf_lsa_header *)(ps+1);
  area=htonl(ps->ospf_packet.areaid);
  oa=ospf_find_area((struct proto_ospf *)p,area);
  for(i=0;i<ntohl(ps->lsano);i++,
    lsa=(struct ospf_lsa_header *)(((u8 *)lsa)+ntohs(lsa->length)))
  {
    struct ospf_lsa_header lsatmp;
    struct top_hash_entry *lsadb;
    if(lsa->checksum!=lsasum_check(lsa,NULL,po))
    {
      log("Received bad lsa checksum from %u\n",n->rid);
      continue;
    }
    if((lsa->type<LSA_T_RT)||(lsa->type>LSA_T_EXT))
    {
      log("Unknown LSA type from %u\n",n->rid);
      continue;
    }
    if((lsa->type==LSA_T_EXT)&&oa->stub)
    {
      log("Received External LSA in stub area from %u\n",n->rid);
      continue;
    }
    ntohlsah(lsa,&lsatmp);
    DBG("Processing update Type: %u ID: %u RT: %u\n",lsa->type,
        ntohl(lsa->id), ntohl(lsa->rt));
    lsadb=ospf_hash_find_header(oa->gr, &lsatmp);
    if((lsatmp.age==LSA_MAXAGE)&&(lsadb==NULL))
    {
      struct ospf_neighbor *n=NULL;
      struct ospf_iface *ifa=NULL;
      int flag=0;

      WALK_LIST(NODE ifa,po->iface_list)
        WALK_LIST(NODE n,ifa->neigh_list)
          if((n->state==NEIGHBOR_EXCHANGE)&&(n->state==NEIGHBOR_LOADING))
            flag=1;

      if(flag==0)
      {
        add_ack_list(n,lsa);
	continue;
      }
    }
    if((lsadb==NULL)||(lsa_comp(&lsatmp,&lsadb->lsa)==CMP_NEWER))
    {
       struct ospf_neighbor *n=NULL;
       struct ospf_iface *ifa=NULL;

       /* FIXME self originated? */

      if(lsadb && ((lsadb->inst_t-now)<MINLSARRIVAL)) continue;

      /* Remove old from all ret lists */
      WALK_LIST(NODE ifa,po->iface_list)
        WALK_LIST(NODE n,ifa->neigh_list)
	{
	  struct top_hash_entry *en;
	  if((en=ospf_hash_find_header(n->lsrth,&lsadb->lsa))!=NULL)
	    s_rem_node(SNODE en);
	}

      /* Install new */
      memcpy(&lsadb->lsa,&lsatmp,sizeof(struct ospf_lsa_header));
      lsadb->inst_t=now;
      if(lsadb->lsa_body!=NULL) mb_free(lsadb->lsa_body);
      lsadb->lsa_body=mb_alloc(p->pool,lsadb->lsa.length-
        sizeof(struct ospf_lsa_header));
      ntohlsab(lsa+1,lsadb->lsa_body,lsadb->lsa.type,lsadb->lsa.length);

      /* FIXME lsa_flood(n,lsadb) */
      /* FIXME ack_lsa() */
      continue;
    }

    /* FIXME pg144 (6)?? */

    if(lsa_comp(&lsatmp,&lsadb->lsa)==CMP_SAME)
    {
	struct top_hash_entry *en;
	if((en=ospf_hash_find_header(n->lsrth,&lsadb->lsa))!=NULL)
	  s_rem_node(SNODE en);
        /* FIXME ack_lsa() */
	continue;
    }

    if((lsadb->lsa.age==LSA_MAXAGE)&&(lsadb->lsa.sn==LSA_MAXSEQNO)) continue;

    /* FIXME lsa_send(n,lsa) */
  }
}

