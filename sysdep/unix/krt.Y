/*
 *	BIRD -- UNIX Kernel Syncer Configuration
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

#include "lib/krt.h"

#define THIS_KRT ((struct krt_config *) this_proto)

CF_DECLS

CF_KEYWORDS(KERNEL, PERSIST, SCAN, TIME, ROUTE, LEARN)

CF_GRAMMAR

/* Kernel protocol */

CF_ADDTO(proto, kern_proto '}')

kern_proto_start: proto_start KERNEL {
     if (!(this_proto = cf_krt)) cf_error("Kernel protocol already defined");
     cf_krt = NULL;
   }
 ;

CF_ADDTO(kern_proto, kern_proto_start '{')
CF_ADDTO(kern_proto, kern_proto proto_item ';')
CF_ADDTO(kern_proto, kern_proto kern_item ';')

kern_item:
   PERSIST bool { THIS_KRT->persist = $2; }
 | SCAN TIME expr {
      /* Scan time of 0 means scan on startup only */
      THIS_KRT->scan_time = $3;
   }
 | ROUTE SCAN TIME expr { THIS_KRT->route_scan_time = $4; }
 | LEARN bool { THIS_KRT->learn = $2; }
 ;

CF_CODE

CF_END
