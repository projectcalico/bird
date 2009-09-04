/*
 *	BIRD -- OSPF
 *
 *	(c) 1999--2004 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "ospf.h"

void
flush_lsa(struct top_hash_entry *en, struct proto_ospf *po)
{
  struct proto *p = &po->proto;

  OSPF_TRACE(D_EVENTS,
	     "Going to remove node Type: %u, Id: %R, Rt: %R, Age: %u, SN: 0x%x",
	     en->lsa.type, en->lsa.id, en->lsa.rt, en->lsa.age, en->lsa.sn);
  s_rem_node(SNODE en);
  if (en->lsa_body != NULL)
    mb_free(en->lsa_body);
  en->lsa_body = NULL;
  ospf_hash_delete(po->gr, en);
}

/**
 * ospf_age
 * @po: ospf protocol
 *
 * This function is periodicaly invoked from ospf_disp(). It computes the new
 * age of all LSAs and old (@age is higher than %LSA_MAXAGE) LSAs are flushed
 * whenever possible. If an LSA originated by the router itself is older
 * than %LSREFRESHTIME a new instance is originated.
 *
 * The RFC says that a router should check the checksum of every LSA to detect
 * hardware problems. BIRD does not do this to minimalize CPU utilization.
 *
 * If routing table calculation is scheduled, it also invalidates the old routing
 * table calculation results.
 */
void
ospf_age(struct proto_ospf *po)
{
  struct proto *p = &po->proto;
  struct top_hash_entry *en, *nxt;
  int flush = can_flush_lsa(po);

  if (po->cleanup) OSPF_TRACE(D_EVENTS, "Running ospf_age cleanup");

  WALK_SLIST_DELSAFE(en, nxt, po->lsal)
  {
    if (po->cleanup)
    {
      en->color = OUTSPF;
      en->dist = LSINFINITY;
      en->nhi = NULL;
      en->nh = IPA_NONE;
      en->lb = IPA_NONE;
      DBG("Infinitying Type: %u, Id: %R, Rt: %R\n", en->lsa.type,
	  en->lsa.id, en->lsa.rt);
    }
    if (en->lsa.age == LSA_MAXAGE)
    {
      if (flush)
	flush_lsa(en, po);
      continue;
    }
    if ((en->lsa.rt == p->cf->global->router_id) &&
	(en->lsa.age >= LSREFRESHTIME))
    {
      OSPF_TRACE(D_EVENTS, "Refreshing my LSA: Type: %u, Id: %R, Rt: %R",
		 en->lsa.type, en->lsa.id, en->lsa.rt);
      en->lsa.sn++;
      en->lsa.age = 0;
      en->inst_t = now;
      en->ini_age = 0;
      lsasum_calculate(&en->lsa, en->lsa_body);
      ospf_lsupd_flood(po, NULL, NULL, &en->lsa, en->domain, 1);
      continue;
    }
    if ((en->lsa.age = (en->ini_age + (now - en->inst_t))) >= LSA_MAXAGE)
    {
      if (flush)
      {
	flush_lsa(en, po);
	schedule_rtcalc(po);
      }
      else
	en->lsa.age = LSA_MAXAGE;
    }
  }
  po->cleanup = 0;
}

void
htonlsah(struct ospf_lsa_header *h, struct ospf_lsa_header *n)
{
  n->age = htons(h->age);
#ifdef OSPFv2
  n->options = h->options;
#endif
  n->type = htont(h->type);
  n->id = htonl(h->id);
  n->rt = htonl(h->rt);
  n->sn = htonl(h->sn);
  n->checksum = htons(h->checksum);
  n->length = htons(h->length);
};

void
ntohlsah(struct ospf_lsa_header *n, struct ospf_lsa_header *h)
{
  h->age = ntohs(n->age);
#ifdef OSPFv2
  h->options = n->options;
#endif
  h->type = ntoht(n->type);
  h->id = ntohl(n->id);
  h->rt = ntohl(n->rt);
  h->sn = ntohl(n->sn);
  h->checksum = ntohs(n->checksum);
  h->length = ntohs(n->length);
};

void
htonlsab(void *h, void *n, u16 type, u16 len)
{
  unsigned int i;

  switch (type)
  {
  case LSA_T_RT:
    {
      struct ospf_lsa_rt *hrt, *nrt;
      struct ospf_lsa_rt_link *hrtl, *nrtl;
      u16 links;

      nrt = n;
      hrt = h;

#ifdef OSPFv2
      links = hrt->links;
      nrt->options = htons(hrt->options);
      nrt->links = htons(hrt->links);
#else /* OSPFv3 */
      nrt->options = htonl(hrt->options);
      links = (len - sizeof(struct ospf_lsa_rt)) /
	sizeof(struct ospf_lsa_rt_link);
#endif

      nrtl = (struct ospf_lsa_rt_link *) (nrt + 1);
      hrtl = (struct ospf_lsa_rt_link *) (hrt + 1);
      for (i = 0; i < links; i++)
      {
#ifdef OSPFv2
	nrtl[i].id = htonl(hrtl[i].id);
	nrtl[i].data = htonl(hrtl[i].data);
	nrtl[i].type = hrtl[i].type;
	nrtl[i].notos = hrtl[i].notos;
	nrtl[i].metric = htons(hrtl[i].metric);
#else /* OSPFv3 */
	nrtl[i].type = hrtl[i].type;
	nrtl[i].padding = 0;
	nrtl[i].metric = htons(hrtl[i].metric);
	nrtl[i].lif = htonl(hrtl[i].lif);
	nrtl[i].nif = htonl(hrtl[i].nif);
	nrtl[i].id = htonl(hrtl[i].id);
#endif
      }
      break;
    }
  case LSA_T_NET:
  case LSA_T_SUM_NET:
  case LSA_T_SUM_RT:
  case LSA_T_EXT:
#ifdef OSPFv3
  case LSA_T_LINK:
  case LSA_T_PREFIX:
#endif
    {
      u32 *hid, *nid;

      nid = n;
      hid = h;

      for (i = 0; i < (len / sizeof(u32)); i++)
      {
	*(nid + i) = htonl(*(hid + i));
      }
      break;
    }

  default:
    bug("(hton): Unknown LSA");
  }
};

void
ntohlsab(void *n, void *h, u16 type, u16 len)
{
  unsigned int i;
  switch (type)
  {
  case LSA_T_RT:
    {
      struct ospf_lsa_rt *hrt, *nrt;
      struct ospf_lsa_rt_link *hrtl, *nrtl;
      u16 links;

      nrt = n;
      hrt = h;

#ifdef OSPFv2
      hrt->options = ntohs(nrt->options);
      links = hrt->links = ntohs(nrt->links);
#else /* OSPFv3 */
      hrt->options = ntohl(nrt->options);
      links = (len - sizeof(struct ospf_lsa_rt)) /
	sizeof(struct ospf_lsa_rt_link);
#endif

      nrtl = (struct ospf_lsa_rt_link *) (nrt + 1);
      hrtl = (struct ospf_lsa_rt_link *) (hrt + 1);
      for (i = 0; i < links; i++)
      {
#ifdef OSPFv2
	hrtl[i].id = ntohl(nrtl[i].id);
	hrtl[i].data = ntohl(nrtl[i].data);
	hrtl[i].type = nrtl[i].type;
	hrtl[i].notos = nrtl[i].notos;
	hrtl[i].metric = ntohs(nrtl[i].metric);
#else /* OSPFv3 */
	hrtl[i].type = nrtl[i].type;
	hrtl[i].padding = 0;
	hrtl[i].metric = ntohs(nrtl[i].metric);
	hrtl[i].lif = ntohl(nrtl[i].lif);
	hrtl[i].nif = ntohl(nrtl[i].nif);
	hrtl[i].id = ntohl(nrtl[i].id);
#endif
      }
      break;
    }
  case LSA_T_NET:
  case LSA_T_SUM_NET:
  case LSA_T_SUM_RT:
  case LSA_T_EXT:
#ifdef OSPFv3
  case LSA_T_LINK:
  case LSA_T_PREFIX:
#endif
    {
      u32 *hid, *nid;

      hid = h;
      nid = n;

      for (i = 0; i < (len / sizeof(u32)); i++)
      {
	hid[i] = ntohl(nid[i]);
      }
      break;
    }
  default:
    bug("(ntoh): Unknown LSA");
  }
};

void
buf_dump(const char *hdr, const byte *buf, int blen)
{
  char b2[1024];
  char *bp;
  int first = 1;
  int i;

  const char *lhdr = hdr;

  bp = b2;
  for(i = 0; i < blen; i++)
    {
      if ((i > 0) && ((i % 16) == 0))
	{
	      *bp = 0;
	      log(L_WARN "%s\t%s", lhdr, b2);
	      lhdr = "";
	      bp = b2;
	}

      bp += snprintf(bp, 1022, "%02x ", buf[i]);

    }

  *bp = 0;
  log(L_WARN "%s\t%s", lhdr, b2);
}

#define MODX 4102		/* larges signed value without overflow */

/* Fletcher Checksum -- Refer to RFC1008. */
#define MODX                 4102
#define LSA_CHECKSUM_OFFSET    15

/* FIXME This is VERY uneficient, I have huge endianity problems */
void
lsasum_calculate(struct ospf_lsa_header *h, void *body)
{
  u16 length = h->length;
  u16 type = h->type;

  //  log(L_WARN "Checksum %R %R %d start (len %d)", h->id, h->rt, h->type, length);
  htonlsah(h, h);
  htonlsab(body, body, type, length - sizeof(struct ospf_lsa_header));

  /*
  char buf[1024];
  memcpy(buf, h, sizeof(struct ospf_lsa_header));
  memcpy(buf + sizeof(struct ospf_lsa_header), body, length - sizeof(struct ospf_lsa_header));
  buf_dump("CALC", buf, length);
  */

  (void) lsasum_check(h, body);

  //  log(L_WARN "Checksum result %4x", h->checksum);

  ntohlsah(h, h);
  ntohlsab(body, body, type, length - sizeof(struct ospf_lsa_header));
}

/*
 * Note, that this function expects that LSA is in big endianity
 * It also returns value in big endian
 */
u16
lsasum_check(struct ospf_lsa_header *h, void *body)
{
  u8 *sp, *ep, *p, *q, *b;
  int c0 = 0, c1 = 0;
  int x, y;
  u16 length;

  b = body;
  sp = (char *) h;
  sp += 2; /* Skip Age field */
  length = ntohs(h->length) - 2;
  h->checksum = 0;

  for (ep = sp + length; sp < ep; sp = q)
  {				/* Actually MODX is very large, do we need the for-cyclus? */
    q = sp + MODX;
    if (q > ep)
      q = ep;
    for (p = sp; p < q; p++)
    {
      /* 
       * I count with bytes from header and than from body
       * but if there is no body, it's appended to header
       * (probably checksum in update receiving) and I go on
       * after header
       */
      if ((b == NULL) || (p < (u8 *) (h + 1)))
      {
	c0 += *p;
      }
      else
      {
	c0 += *(b + (p - sp) - sizeof(struct ospf_lsa_header) + 2);
      }

      c1 += c0;
    }
    c0 %= 255;
    c1 %= 255;
  }

  x = ((length - LSA_CHECKSUM_OFFSET) * c0 - c1) % 255;
  if (x <= 0)
    x += 255;
  y = 510 - c0 - x;
  if (y > 255)
    y -= 255;

  ((u8 *) & h->checksum)[0] = x;
  ((u8 *) & h->checksum)[1] = y;
  return h->checksum;
}

int
lsa_comp(struct ospf_lsa_header *l1, struct ospf_lsa_header *l2)
			/* Return codes from point of view of l1 */
{
  u32 sn1, sn2;

  sn1 = l1->sn - LSA_INITSEQNO + 1;
  sn2 = l2->sn - LSA_INITSEQNO + 1;

  if (sn1 > sn2)
    return CMP_NEWER;
  if (sn1 < sn2)
    return CMP_OLDER;

  if (l1->checksum != l2->checksum)
    return l1->checksum < l2->checksum ? CMP_OLDER : CMP_NEWER;

  if ((l1->age == LSA_MAXAGE) && (l2->age != LSA_MAXAGE))
    return CMP_NEWER;
  if ((l2->age == LSA_MAXAGE) && (l1->age != LSA_MAXAGE))
    return CMP_OLDER;

  if (ABS(l1->age - l2->age) > LSA_MAXAGEDIFF)
    return l1->age < l2->age ? CMP_NEWER : CMP_OLDER;

  return CMP_SAME;
}

/**
 * lsa_install_new - install new LSA into database
 * @po: OSPF protocol
 * @lsa: LSA header
 * @domain: domain of LSA
 * @body: pointer to LSA body

 *
 * This function ensures installing new LSA into LSA database. Old instance is
 * replaced. Several actions are taken to detect if new routing table
 * calculation is necessary. This is described in 13.2 of RFC 2328.
 */
struct top_hash_entry *
lsa_install_new(struct proto_ospf *po, struct ospf_lsa_header *lsa, u32 domain, void *body)
{
  /* LSA can be temporarrily, but body must be mb_allocated. */
  int change = 0;
  struct top_hash_entry *en;

  if ((en = ospf_hash_find_header(po->gr, domain, lsa)) == NULL)
  {
    en = ospf_hash_get_header(po->gr, domain, lsa);
    change = 1;
  }
  else
  {
    if ((en->lsa.length != lsa->length)
#ifdef OSPFv2       
	|| (en->lsa.options != lsa->options)
#endif
	|| (en->lsa.age == LSA_MAXAGE)
	|| (lsa->age == LSA_MAXAGE)
	|| memcmp(en->lsa_body, body, lsa->length - sizeof(struct ospf_lsa_header)))
      change = 1;

    s_rem_node(SNODE en);
  }

  DBG("Inst lsa: Id: %R, Rt: %R, Type: %u, Age: %u, Sum: %u, Sn: 0x%x\n",
      lsa->id, lsa->rt, lsa->type, lsa->age, lsa->checksum, lsa->sn);

  s_add_tail(&po->lsal, SNODE en);
  en->inst_t = now;
  if (en->lsa_body != NULL)
    mb_free(en->lsa_body);
  en->lsa_body = body;
  memcpy(&en->lsa, lsa, sizeof(struct ospf_lsa_header));
  en->ini_age = en->lsa.age;

  if (change)
  {
    schedule_rtcalc(po);
  }

  return en;
}
