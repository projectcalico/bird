/*
 *	BIRD -- OSPF
 *
 *	(c) 2000 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

int
flood_lsa(struct ospf_neighbor *n, struct ospf_lsa_header *hn,
  struct ospf_lsa_header *hh, struct proto_ospf *po, struct ospf_iface *iff,
  struct ospf_area *oa, int rtl)
{
  struct ospf_iface *ifa;
  struct ospf_neighbor *nn;
  struct top_hash_entry *en;
  struct proto *p=&po->proto;
  int ret,retval=0;

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
      if(oa->areaid==BACKBONE)
      {
        if((ifa->type!=OSPF_IT_VLINK)&&(ifa->oa!=oa)) continue;
      }
      else
      {
        if(ifa->oa!=oa) continue;
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
	      DBG("Removing from lsreq list for neigh %I\n", nn->rid);
	      ospf_hash_delete(nn->lsrqh,en);
	      if(EMPTY_SLIST(nn->lsrql)) ospf_neigh_sm(nn, INM_LOADDONE);
	      continue;
	      break;
	    case CMP_NEWER:
              s_rem_node(SNODE en);
	      DBG("Removing from lsreq list for neigh %I\n", nn->rid);
	      ospf_hash_delete(nn->lsrqh,en);
	      if(EMPTY_SLIST(nn->lsrql)) ospf_neigh_sm(nn, INM_LOADDONE);
	      break;
	    default: bug("Bug in lsa_comp?\n");
	  }
	}
      }
      if(nn==n) continue;
      if(rtl!=0)
      {
        if((en=ospf_hash_find_header(nn->lsrth, hh))==NULL)
        {
          en=ospf_hash_get_header(nn->lsrth, hh);
        }
        else
        {
          s_rem_node(SNODE en);
        }
        s_add_tail(&nn->lsrtl, SNODE en);
        memcpy(&en->lsa,hh,sizeof(struct ospf_lsa_header));
        DBG("Adding LSA lsrt RT: %I, Id: %I, Type: %u for n: %I\n",
          en->lsa.rt,en->lsa.id, en->lsa.type, nn->ip);
      }
      else
      {
        if((en=ospf_hash_find_header(nn->lsrth, hh))!=NULL)
        {
          s_rem_node(SNODE en);
          ospf_hash_delete(nn->lsrth, en);
        }
      }
      ret=1;
    }

    if(ret==0) continue;

    if(ifa==iff)
    {
      if((n->rid==iff->drid)||n->rid==iff->bdrid) continue;
      if(iff->state==OSPF_IS_BACKUP) continue;
      retval=1;
    }

    {
      sock *sk;
      ip_addr to;
      u16 len;
      struct ospf_lsupd_packet *pk;
      struct ospf_packet *op;

      if(ifa->type==OSPF_IT_NBMA)  sk=ifa->ip_sk;
      else sk=ifa->hello_sk;	/* FIXME is this true for PTP? */

      pk=(struct ospf_lsupd_packet *)sk->tbuf;
      op=(struct ospf_packet *)sk->tbuf;

      fill_ospf_pkt_hdr(ifa, pk, LSUPD);
      pk->lsano=htonl(1);
      if(hn!=NULL)
      {
        memcpy(pk+1,hn,ntohs(hn->length));
        len=sizeof(struct ospf_lsupd_packet)+ntohs(hn->length);
      }
      else
      {
        u8 *help;
	struct top_hash_entry *en;
	struct ospf_lsa_header *lh;
	u16 age;

	lh=(struct ospf_lsa_header *)(pk+1);
        htonlsah(hh,lh);
	age=hh->age;
	age+=ifa->inftransdelay;
	if(age>LSA_MAXAGE) age=LSA_MAXAGE;
	lh->age=htons(age);
	help=(u8 *)(lh+1);
	en=ospf_hash_find_header(oa->gr,hh);
	htonlsab(en->lsa_body,help,hh->type,hh->length
          -sizeof(struct ospf_lsa_header));
	len=hh->length+sizeof(struct ospf_lsupd_packet);
      }
      op->length=htons(len);
      ospf_pkt_finalize(ifa, op);
      debug("%s: LS upd flooded via %s\n", p->name, ifa->iface->name);

      if(ifa->type==OSPF_IT_NBMA)
      {
        sk_send_to_agt(sk ,len, ifa, NEIGHBOR_EXCHANGE);
      }
      else
      {
        if((ifa->state==OSPF_IS_BACKUP)||(ifa->state==OSPF_IS_DR))
          sk_send_to(sk,len, AllSPFRouters, OSPF_PROTO);
	else sk_send_to(sk,len, AllDRouters, OSPF_PROTO);
      }
    }
  }
  return retval;
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
  struct proto *p=&n->ifa->oa->po->proto;
  void *pktpos;
  u8 ii;
  u8 *jj=n->ifa->ip_sk->tbuf;

  if(EMPTY_LIST(*l)) return;

  pk=(struct ospf_lsupd_packet *)n->ifa->ip_sk->tbuf;
  op=(struct ospf_packet *)n->ifa->ip_sk->tbuf;

  DBG("LSupd: 1st packet\n");
       
  fill_ospf_pkt_hdr(n->ifa, pk, LSUPD);
  len=SIPH+sizeof(struct ospf_lsupd_packet);
  lsano=0;
  pktpos=(pk+1);

  WALK_LIST(llsh, *l)
  {
    if((en=ospf_hash_find(n->ifa->oa->gr,llsh->lsh.id,llsh->lsh.rt,
      llsh->lsh.type))==NULL) continue;		/* Probably flushed LSA */

    DBG("Sending ID=%I, Type=%u, RT=%I\n", llsh->lsh.id, llsh->lsh.type,
      llsh->lsh.rt);
    if(((u32)(len+en->lsa.length))>n->ifa->iface->mtu)
    {
      pk->lsano=htonl(lsano);
      op->length=htons(len-SIPH);
      ospf_pkt_finalize(n->ifa, op);
		       
      sk_send_to(n->ifa->ip_sk,len-SIPH, n->ip, OSPF_PROTO);
      debug("%s: LS upd sent to %I (%d LSAs)\n", p->name, n->ip, lsano);

      DBG("LSupd: next packet\n");
      fill_ospf_pkt_hdr(n->ifa, pk, LSUPD);
      len=SIPH+sizeof(struct ospf_lsupd_packet);
      lsano=0;
      pktpos=(pk+1);
    }
    htonlsah(&(en->lsa), pktpos);
    pktpos=pktpos+sizeof(struct ospf_lsa_header);
    htonlsab(en->lsa_body, pktpos, en->lsa.type, en->lsa.length
      -sizeof(struct ospf_lsa_header));
    pktpos=pktpos+en->lsa.length-sizeof(struct ospf_lsa_header);
    len+=en->lsa.length;
    lsano++;
  }
  if(lsano>0)
  {
    pk->lsano=htonl(lsano);
    op->length=htons(len-SIPH);
    ospf_pkt_finalize(n->ifa, op);

    debug("%s: LS upd sent to %I (%d LSAs)\n", p->name, n->ip, lsano);
    sk_send_to(n->ifa->ip_sk,len-SIPH, n->ip, OSPF_PROTO);
  }
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
    debug("%s: Received lsupd from unknown neigbor! (%I)\n", p->name,
      nrid);
    return ;
  }
  if(n->state<NEIGHBOR_EXCHANGE)
  {
    debug("%s: Received lsupd in lesser state than EXCHANGE from (%I)\n",
      p->name,n->ip);
    return;
  }
  if(size<=(sizeof(struct ospf_lsupd_packet)+sizeof(struct ospf_lsa_header)))
  {
    log("%s: Received lsupd from %I is too short!\n", p->name,n->ip);
    return;
  }

  debug("%s: Received LS upd from %I\n", p->name, n->ip); 

  lsa=(struct ospf_lsa_header *)(ps+1);
  area=htonl(ps->ospf_packet.areaid);
  oa=ospf_find_area((struct proto_ospf *)p,area);
  for(i=0;i<ntohl(ps->lsano);i++,
    lsa=(struct ospf_lsa_header *)(((u8 *)lsa)+ntohs(lsa->length)))
  {
    struct ospf_lsa_header lsatmp;
    struct top_hash_entry *lsadb;
    u16 lenn;
    int diff=((u8 *)lsa)-((u8 *)ps);

    if(((diff+sizeof(struct ospf_lsa_header))>=size) ||
      ((ntohs(lsa->length)+diff)>size))
      log("%s: Received lsupd from %I is too short.\n", p->name,n->ip);

    lenn=ntohs(lsa->length);

    if((lenn<=sizeof(struct ospf_lsa_header))||(lenn!=(4*(lenn/4))))
    {
      log("Received LSA with bad length\n");
      ospf_neigh_sm(n,INM_BADLSREQ);
      break;
    }
    /* pg 143 (1) */
    if(lsa->checksum!=lsasum_check(lsa,NULL,po))
    {
      log("Received bad lsa checksum from %I\n",n->rid);
      continue;
    }
    /* pg 143 (2) */
    if((lsa->type<LSA_T_RT)||(lsa->type>LSA_T_EXT))
    {
      log("Unknown LSA type from %I\n",n->rid);
      continue;
    }
    /* pg 143 (3) */
    if((lsa->type==LSA_T_EXT)&&oa->stub)
    {
      log("Received External LSA in stub area from %I\n",n->rid);
      continue;
    }
    ntohlsah(lsa,&lsatmp);
    DBG("Processing update Type: %u ID: %I RT: %I, Sn: 0x%08x\n",lsatmp.type,
      lsatmp.id, lsatmp.rt, lsatmp.sn);
    lsadb=ospf_hash_find_header(oa->gr, &lsatmp);

    /* pg 143 (4) */
    if((lsatmp.age==LSA_MAXAGE)&&(lsadb==NULL))
    {
      int flag=0;
      struct ospf_iface *ifatmp;

      WALK_LIST(NODE ifatmp,po->iface_list)
        WALK_LIST(NODE ntmp,ifatmp->neigh_list)
          if((ntmp->state==NEIGHBOR_EXCHANGE)&&
            (ntmp->state==NEIGHBOR_LOADING))
            flag=1;
      DBG("PG143(4): Flag=%u\n",flag);

      if(flag==0)
      {
        ospf_lsack_direct_tx(n,lsa);
	continue;
      }
    }
    /* pg 144 (5) */
    if((lsadb==NULL)||(lsa_comp(&lsatmp,&lsadb->lsa)==CMP_NEWER))
    {
       struct ospf_iface *ift=NULL;
       void *body;
       struct ospf_iface *nifa;
       int self=0;

       DBG("PG143(5): Received LSA is newer\n");

       if(lsatmp.rt==p->cf->global->router_id) self=1;

       if(lsatmp.type==LSA_T_NET)
       {
         WALK_LIST(nifa,po->iface_list)
	 {
	   if(ipa_compare(nifa->iface->addr->ip,ipa_from_u32(lsatmp.id))==0)
	   {
	     self=1;
	     break;
	   }
	 }
       }

       if(self)
       {
         struct top_hash_entry *en;

         if((lsatmp.age==LSA_MAXAGE)&&(lsatmp.sn==LSA_MAXSEQNO)) continue;

	 lsatmp.age=LSA_MAXAGE;
	 lsatmp.sn=LSA_MAXSEQNO;
         lsa->age=htons(LSA_MAXAGE);
         lsa->sn=htonl(LSA_MAXSEQNO);
	 debug("%s: Premature aging self originated lsa.\n",p->name);
         debug("%s: Type: %d, Id: %I, Rt: %I\n", p->name, lsatmp.type,
           lsatmp.id, lsatmp.rt);
	 lsasum_check(lsa,(lsa+1),po);
	 lsatmp.checksum=ntohs(lsa->checksum);
         flood_lsa(NULL,lsa,&lsatmp,po,NULL,oa,0);
	 if(en=ospf_hash_find_header(oa->gr,&lsatmp))
	 {
           flood_lsa(NULL,NULL,&en->lsa,po,NULL,oa,1);
	 }
	 continue;
       }

      /* pg 144 (5a) */
      if(lsadb && ((now-lsadb->inst_t)<=MINLSARRIVAL))
      {
        DBG("I got it in less that MINLSARRIVAL\n");
	continue;
      }
        
      if(flood_lsa(n,lsa,&lsatmp,po,ifa,ifa->oa,1)==0)
      {
        DBG("Wasn't flooded back\n");
        if(ifa->state==OSPF_IS_BACKUP)
	{
	  if(ifa->drid==n->rid) ospf_lsa_delay(n, lsa, p);
	}
	else ospf_lsa_delay(n, lsa, p);
      }

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
      lsadb=lsa_install_new(&lsatmp,body, oa, p);
      DBG("New LSA installed in DB\n");

      continue;
    }

    /* FIXME pg145 (6)?? */

    /* pg145 (7) */
    if(lsa_comp(&lsatmp,&lsadb->lsa)==CMP_SAME)
    {
      struct top_hash_entry *en;
      DBG("PG145(6) Got the same LSA\n");
      if((en=ospf_hash_find_header(n->lsrth,&lsadb->lsa))!=NULL)
      {
        s_rem_node(SNODE en);
        ospf_hash_delete(n->lsrth, en);
        if(ifa->state==OSPF_IS_BACKUP)
        {
          if(n->rid==ifa->drid) ospf_lsa_delay(n, lsa, p);
        }
      }
      else
      {
        ospf_lsack_direct_tx(n,lsa);
      }
      continue;
    }

    /* pg145 (8) */
    if((lsadb->lsa.age==LSA_MAXAGE)&&(lsadb->lsa.sn==LSA_MAXSEQNO))
    {
      continue;
    }

    {
      list l;
      struct l_lsr_head ll;
      init_list(&l);
      ll.lsh.id=lsadb->lsa.id;
      ll.lsh.rt=lsadb->lsa.rt;
      ll.lsh.type=lsadb->lsa.type;
      add_tail(&l, NODE &ll);
      ospf_lsupd_tx_list(n, &l);
    }
  }
  if(n->state==NEIGHBOR_LOADING)
  {
    ospf_lsreq_tx(n);	/* Send me another part of database */
  }
}

void
net_flush_lsa(struct top_hash_entry *en, struct proto_ospf *po,
  struct ospf_area *oa)
{
  struct ospf_lsa_header *lsa=&en->lsa;

  lsa->age=LSA_MAXAGE;
  lsa->sn=LSA_MAXSEQNO;
  lsasum_calculate(lsa,en->lsa_body,po);
  debug("%s: Premature aging self originated lsa!\n",po->proto.name);
  debug("%s: Type: %d, Id: %I, Rt: %I\n", po->proto.name, lsa->type,
    lsa->id, lsa->rt);
  flood_lsa(NULL,NULL,lsa,po,NULL,oa,0);
}

