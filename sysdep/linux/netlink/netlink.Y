/*
 *	BIRD -- Linux Netlink Configuration
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

CF_DECLS

CF_KEYWORDS(ASYNC)

CF_GRAMMAR

CF_ADDTO(kern_proto, kern_proto nl_item ';')

nl_item:
   ASYNC bool { THIS_KRT->scan.async = $2; }
 ;

CF_CODE

CF_END
