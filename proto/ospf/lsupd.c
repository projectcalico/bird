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

void
flood_lsa(struct ospf_neighbor *n, struct ospf_lsa_header *hn,
  struct ospf_lsa_header *hh, struct proto_ospf *po, struct ospf_iface *iff)
{
  struct ospf_iface *ifa;
  struct ospf_neighbor *nn;
  struct top_hash_entry *en;
  int ret;

  /* pg 148 */
  WALK_LIST(NODE ifa,po->iface_list)
  {
    if(hh->type==LSA_T_EXT)
    {
      if(ifa->type==OSPF_IT_VLINK) continue;
      if(ifa->oa->stub) continue;
    }
    else
    {
      if(iff->oa->areaid==BACKBONE)
      {
        if((ifa->type!=OSPF_IT_VLINK)&&(ifa->oa!=iff->oa)) continue;
      }
      else
      {
        if(ifa->oa!=iff->oa) continue;
      }
    }
    ret=0;
    WALK_LIST(NODE nn, ifa->neigh_list)
    {
      if(nn->state<NEIGHBOR_EXCHANGE) continue;
      if(nn->state<NEIGHBOR_FULL)
      {

        if((en=ospf_hash_find_header(nn->lsrqh,hh))!=NULL)
	{
	  switch(lsa_comp(hh,&en->lsa))
	  {
            case CMP_OLDER:
              continue;
	      break;
	    case CMP_SAME:
              s_rem_node(SNODE en);
	      DBG("Removing from lsreq list for neigh %u\n", nn->rid);
	      ospf_hash_delete(nn->lsrqh,en);
	      if(EMPTY_SLIST(nn->lsrql)) ospf_neigh_sm(nn, INM_LOADDONE);
	      continue;
	      break;
	    case CMP_NEWER:
              s_rem_node(SNODE en);
	      DBG("Removing from lsreq list for neigh %u\n", nn->rid);
	      ospf_hash_delete(nn->lsrqh,en);
	      if(EMPTY_SLIST(nn->lsrql)) ospf_neigh_sm(nn, INM_LOADDONE);
	      break;
	    default: bug("Bug in lsa_comp?\n");
	  }
	}
      }
      if(nn==n) continue;
      en=ospf_hash_get_header(nn->lsrth, hh);
      s_add_tail(&nn->lsrtl, SNODE en);
      ret=1;
    }
    if(ret==0) continue;
    if(ifa==iff)
    {
      if((n->rid==iff->drid)||n->rid==iff->bdrid) continue;
      if(iff->state=OSPF_IS_BACKUP) continue;
    }
    /* FIXME directly flood */
    {
      sock *sk;
      ip_addr to;
      u16 len;
      struct ospf_lsupd_packet *pk;
      struct ospf_packet *op;

      if(ifa->type==OSPF_IT_NBMA)  sk=iff->ip_sk;
      else sk=iff->hello_sk;	/* FIXME is this tru for PTP? */

      pk=(struct ospf_lsupd_packet *)sk->tbuf;
      op=(struct ospf_packet *)sk->tbuf;

      fill_ospf_pkt_hdr(ifa, pk, LSUPD);
      pk->lsano=htonl(1);
      memcpy(pk+1,hn,ntohs(hn->length));
      len=sizeof(struct ospf_lsupd_packet)+ntohs(hn->length);
      op->length=htons(len);
      ospf_pkt_finalize(ifa, op);

      if(ifa->type==OSPF_IT_NBMA)
      {
	struct ospf_neighbor *nnn;
        WALK_LIST(NODE nnn,ifa->neigh_list)
	{
          if(nnn->state>NEIGHBOR_EXSTART)
            sk_send_to(sk,len, nnn->ip, OSPF_PROTO);
	}
      }
      else
      {
        if((ifa->state==OSPF_IS_BACKUP)||(ifa->state==OSPF_IS_DR))
          sk_send_to(sk,len, AllSPFRouters, OSPF_PROTO);
	else sk_send_to(sk,len, AllDRouters, OSPF_PROTO);
      }
    }
  }
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
  struct ospf_neighbor *n,*ntmp;
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
    /* pg 143 (1) */
    if(lsa->checksum!=lsasum_check(lsa,NULL,po))
    {
      log("Received bad lsa checksum from %u\n",n->rid);
      continue;
    }
    /* pg 143 (2) */
    if((lsa->type<LSA_T_RT)||(lsa->type>LSA_T_EXT))
    {
      log("Unknown LSA type from %u\n",n->rid);
      continue;
    }
    /* pg 143 (3) */
    if((lsa->type==LSA_T_EXT)&&oa->stub)
    {
      log("Received External LSA in stub area from %u\n",n->rid);
      continue;
    }
    ntohlsah(lsa,&lsatmp);
    DBG("Processing update Type: %u ID: %u RT: %u\n",lsatmp.type,
        lsatmp.id, lsatmp.rt);
    lsadb=ospf_hash_find_header(oa->gr, &lsatmp);

    /* pg 143 (4) */
    if((lsatmp.age==LSA_MAXAGE)&&(lsadb==NULL))
    {
      struct ospf_neighbor *n=NULL;
      struct ospf_iface *ifa=NULL;
      int flag=0;

      WALK_LIST(NODE ifa,po->iface_list)
        WALK_LIST(NODE ntmp,ifa->neigh_list)
          if((ntmp->state==NEIGHBOR_EXCHANGE)&&
            (ntmp->state==NEIGHBOR_LOADING))
            flag=1;

      if(flag==0)
      {
        add_ack_list(n,lsa);
	continue;
      }
    }
    /* pg 144 (5) */
    if((lsadb==NULL)||(lsa_comp(&lsatmp,&lsadb->lsa)==CMP_NEWER))
    {
       struct ospf_iface *ift=NULL;
       void *body;


      /* pg 144 (5a) */
      if(lsadb && ((lsadb->inst_t-now)<MINLSARRIVAL)) continue;

      flood_lsa(n,lsa,&lsatmp,po,ifa);

      /* Remove old from all ret lists */
      /* pg 144 (5c) */
      if(lsadb)
        WALK_LIST(NODE ift,po->iface_list)
          WALK_LIST(NODE ntmp,ift->neigh_list)
	  {
	    struct top_hash_entry *en;
	    if(ntmp->state>NEIGHBOR_EXSTART)
	      if((en=ospf_hash_find_header(ntmp->lsrth,&lsadb->lsa))!=NULL)
	      {
	        s_rem_node(SNODE en);
	        ospf_hash_delete(ntmp->lsrth,en);
	      }
	  }

      /* pg 144 (5d) */
      body=mb_alloc(p->pool,lsatmp.length-sizeof(struct ospf_lsa_header));
      ntohlsab(lsa+1,body,lsatmp.type,
        lsatmp.length-sizeof(struct ospf_lsa_header));
      lsadb=lsa_install_new(&lsatmp,body, oa);
      DBG("New LSA installed in DB\n");

      /* FIXME 144 (5e) ack */
      /* FIXME 145 (5f) self originated? */

      continue;
    }

    /* FIXME pg145 (6)?? */

    /* pg145 (7) */
    if(lsa_comp(&lsatmp,&lsadb->lsa)==CMP_SAME)
    {
	struct top_hash_entry *en;
	if((en=ospf_hash_find_header(n->lsrth,&lsadb->lsa))!=NULL)
	  s_rem_node(SNODE en);
        /* FIXME ack_lsa() */
	continue;
    }

    /* pg145 (8) */
    if((lsadb->lsa.age==LSA_MAXAGE)&&(lsadb->lsa.sn==LSA_MAXSEQNO)) continue;

    {
      list l;
      struct l_lsr_head llsh;
      init_list(&l);
      memcpy(&llsh.lsh,&lsadb->lsa,sizeof(struct ospf_lsa_header));
      add_tail(&l, NODE &llsh);
      ospf_lsupd_tx_list(n, &l);
    }
  }
}

