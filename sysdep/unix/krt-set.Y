/*
 *	BIRD -- UNIX Kernel Syncer Configuration
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

#include "lib/krt-scan.h"

CF_DECLS

CF_KEYWORDS(PERSIST)

CF_GRAMMAR

CF_ADDTO(kern_proto, kern_proto krt_set_item ';')

krt_set_item:
   PERSIST bool {
      ((struct krt_config *) this_proto)->setopt.persist = $2;
   }
 ;

CF_CODE

CF_END
