/*
 *	BIRD -- UNIX Kernel Syncer Configuration
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

#include "lib/krt.h"

#define THIS_KRT ((struct krt_config *) this_proto)
#define THIS_KIF ((struct kif_config *) this_proto)

CF_DECLS

CF_KEYWORDS(KERNEL, PERSIST, SCAN, TIME, LEARN, DEVICE)

CF_GRAMMAR

/* Kernel syncer protocol */

CF_ADDTO(proto, kern_proto '}')

kern_proto_start: proto_start KERNEL {
     if (cf_krt)
       cf_error("Kernel protocol already defined");
     cf_krt = this_proto = proto_config_new(&proto_unix_kernel, sizeof(struct krt_config));
     this_proto->preference = 0;
     THIS_KRT->scan_time = 60;
     THIS_KRT->learn = THIS_KRT->persist = 0;
     krt_scan_preconfig(THIS_KRT);
     krt_set_preconfig(THIS_KRT);
   }
 ;

CF_ADDTO(kern_proto, kern_proto_start proto_name '{')
CF_ADDTO(kern_proto, kern_proto proto_item ';')
CF_ADDTO(kern_proto, kern_proto kern_item ';')

kern_item:
   PERSIST bool { THIS_KRT->persist = $2; }
 | SCAN TIME expr {
      /* Scan time of 0 means scan on startup only */
      THIS_KRT->scan_time = $3;
   }
 | LEARN bool {
      THIS_KRT->learn = $2;
#ifndef KRT_ALLOW_LEARN
      if ($2)
	cf_error("Learning of kernel routes not supported in this configuration");
#endif
   }
 ;

/* Kernel interface protocol */

CF_ADDTO(proto, kif_proto '}')

kif_proto_start: proto_start DEVICE {
     if (cf_kif)
       cf_error("Kernel device protocol already defined");
     cf_kif = this_proto = proto_config_new(&proto_unix_iface, sizeof(struct kif_config));
     this_proto->preference = DEF_PREF_DIRECT;
     THIS_KIF->scan_time = 60;
     krt_if_preconfig(THIS_KIF);
   }
 ;

CF_ADDTO(kif_proto, kif_proto_start '{')
CF_ADDTO(kif_proto, kif_proto proto_item ';')
CF_ADDTO(kif_proto, kif_proto kif_item ';')

kif_item:
   SCAN TIME expr {
      /* Scan time of 0 means scan on startup only */
      THIS_KIF->scan_time = $3;
   }
 ;

CF_CODE

CF_END
