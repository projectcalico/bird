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
htonlsab(void *h, void *n, u8 type, u16 len)
{
  unsigned int i;
  switch(type)
  {
    case LSA_T_RT:
    {
      struct ospf_lsa_rt *hrt, *nrt;
      struct ospf_lsa_rt_link *hrtl,*nrtl;

      nrt=n;
      hrt=h;

      nrt->VEB=hrt->VEB;
      nrt->padding=0;
      nrt->links=htons(hrt->links);
      nrtl=(struct ospf_lsa_rt_link *)(nrt+1);
      hrtl=(struct ospf_lsa_rt_link *)(hrt+1);
      for(i=0;i<hrt->links;i++)
      {
        (nrtl+i)->id=htonl((hrtl+i)->id);
        (nrtl+i)->data=htonl((hrtl+i)->data);
        (nrtl+i)->type=(hrtl+i)->type;
        (nrtl+i)->notos=(hrtl+i)->notos;
        (nrtl+i)->metric=htons((hrtl+i)->metric);
      }
      break;
    }
    case LSA_T_NET:
    {
      u32 *hid,*nid;

      nid=n;
      hid=h;

      for(i=0;i<(len/sizeof(u32));i++)
      {
        *(nid+i)=htonl(*(hid+i));
      }
      break;
    }
    case LSA_T_SUM_NET:
    case LSA_T_SUM_RT:
    {
      struct ospf_lsa_summ *hs, *ns;
      struct ospf_lsa_summ_net *hn, *nn;

      hs=h;
      ns=n;

      ns->netmask=htonl(hs->netmask);

      hn=(struct ospf_lsa_summ_net *)(hs+1);
      nn=(struct ospf_lsa_summ_net *)(ns+1);

      for(i=0;i<((len-sizeof(struct ospf_lsa_summ))/
        sizeof(struct ospf_lsa_summ_net));i++)
      {
        (nn+i)->tos=(hn+i)->tos;
	(nn+i)->metric=htons((hn+i)->metric);
	(nn+i)->padding=0;
      }
      break;
    }
    case LSA_T_EXT:
    {
      struct ospf_lsa_ext *he, *ne;
      struct ospf_lsa_ext_tos *ht, *nt;

      he=h;
      ne=n;

      ne->netmask=htonl(he->netmask);

      ht=(struct ospf_lsa_ext_tos *)(he+1);
      nt=(struct ospf_lsa_ext_tos *)(ne+1);

      for(i=0;i<((len-sizeof(struct ospf_lsa_ext))/
        sizeof(struct ospf_lsa_ext_tos));i++)
      {
        (nt+i)->etos=(ht+i)->etos;
        (nt+i)->padding=0;
        (nt+i)->metric=htons((ht+i)->metric);
        (nt+i)->fwaddr=htonl((ht+i)->fwaddr);
        (nt+i)->tag=htonl((ht+i)->tag);
      }
      break;
    }
    default: die("(hton): Unknown LSA\n");
  }
};

void
ntohlsab(void *n, void *h, u8 type, u16 len)
{
  unsigned int i;
  switch(type)
  {
    case LSA_T_RT:
    {
      struct ospf_lsa_rt *hrt, *nrt;
      struct ospf_lsa_rt_link *hrtl,*nrtl;

      nrt=n;
      hrt=h;

      hrt->VEB=nrt->VEB;
      hrt->padding=0;
      hrt->links=ntohs(nrt->links);
      nrtl=(struct ospf_lsa_rt_link *)(nrt+1);
      hrtl=(struct ospf_lsa_rt_link *)(hrt+1);
      for(i=0;i<hrt->links;i++)
      {
        (hrtl+i)->id=ntohl((nrtl+i)->id);
        (hrtl+i)->data=ntohl((nrtl+i)->data);
        (hrtl+i)->type=(nrtl+i)->type;
        (hrtl+i)->notos=(nrtl+i)->notos;
        (hrtl+i)->metric=ntohs((nrtl+i)->metric);
      }
      break;
    }
    case LSA_T_NET:
    {
      u32 *hid,*nid;

      hid=h;
      nid=n;

      for(i=0;i<(len/sizeof(u32));i++)
      {
        *(hid+i)=ntohl(*(nid+i));
      }
      break;
    }
    case LSA_T_SUM_NET:
    case LSA_T_SUM_RT:
    {
      struct ospf_lsa_summ *hs, *ns;
      struct ospf_lsa_summ_net *hn, *nn;

      hs=h;
      ns=n;

      hs->netmask=ntohl(ns->netmask);

      hn=(struct ospf_lsa_summ_net *)(hs+1);
      nn=(struct ospf_lsa_summ_net *)(ns+1);

      for(i=0;i<((len-sizeof(struct ospf_lsa_summ))/
        sizeof(struct ospf_lsa_summ_net));i++)
      {
        (hn+i)->tos=(nn+i)->tos;
	(hn+i)->metric=ntohs((nn+i)->metric);
	(hn+i)->padding=0;
      }
      break;
    }
    case LSA_T_EXT:
    {
      struct ospf_lsa_ext *he, *ne;
      struct ospf_lsa_ext_tos *ht, *nt;

      he=h;
      ne=n;

      he->netmask=ntohl(ne->netmask);

      ht=(struct ospf_lsa_ext_tos *)(he+1);
      nt=(struct ospf_lsa_ext_tos *)(ne+1);

      for(i=0;i<((len-sizeof(struct ospf_lsa_ext))/
        sizeof(struct ospf_lsa_ext_tos));i++)
      {
        (ht+i)->etos=(nt+i)->etos;
        (ht+i)->padding=0;
        (ht+i)->metric=ntohs((nt+i)->metric);
        (ht+i)->fwaddr=ntohl((nt+i)->fwaddr);
        (ht+i)->tag=ntohl((nt+i)->tag);
      }
      break;
    }
    default: die("(ntoh): Unknown LSA\n");
  }
};

