/*
 *	BIRD -- UNIX Interface Syncer Configuration
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

#include "lib/krt-scan.h"

CF_DECLS

CF_KEYWORDS(LEARN, ROUTE, SCAN, TIME)

CF_GRAMMAR

CF_ADDTO(kern_proto, kern_proto krt_if_item ';')

krt_if_item:
   SCAN TIME expr {
      /* Scan time of 0 means scan on startup only */
      ((struct krt_config *) this_proto)->ifopt.scan_time = $3;
   }
 ;

CF_CODE

CF_END
