/*
 *      BIRD -- OSPF
 *
 *      (c) 2000 Ondrej Filip <feela@network.cz>
 *
 *      Can be freely distributed and used under the terms of the GNU GPL.
 *
 */

#ifndef _BIRD_OSPF_LSALIB_H_
#define _BIRD_OSPF_LSALIB_H_

void htonlsah(struct ospf_lsa_header *h, struct ospf_lsa_header *n);
void ntohlsah(struct ospf_lsa_header *n, struct ospf_lsa_header *h);
void htonlsab(void *h, void *n, u8 type, u16 len);
void ntohlsab(void *n, void *h, u8 type, u16 len);
void lsasum_calculate(struct ospf_lsa_header *header, void *body, struct proto_ospf *p);

#endif /* _BIRD_OSPF_LSALIB_H_ */
