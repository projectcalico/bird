/*
 *	BIRD -- Linux Netlink Configuration
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

CF_DECLS

CF_KEYWORDS(ASYNC, KERNEL, TABLE)

CF_GRAMMAR

CF_ADDTO(kern_proto, kern_proto nl_item ';')

nl_item:
   ASYNC bool { THIS_KRT->scan.async = $2; }
 | KERNEL TABLE expr {
	if ($3 <= 0 || $3 >= NL_NUM_TABLES)
	  cf_error("Kernel routing table number out of range");
	THIS_KRT->scan.table_id = $3;
   }
 ;

CF_CODE

CF_END
