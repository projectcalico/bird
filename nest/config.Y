/*
 *	BIRD -- Core Configuration
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

static struct proto_config *this_proto;

#include "nest/rt-dev.h"

void rt_dev_add_iface(char *);

CF_DECLS

CF_KEYWORDS(ROUTER, ID, PROTOCOL, PREFERENCE, DISABLED, DEBUG, ALL, OFF, DEVICE)
CF_KEYWORDS(INTERFACE, INPUT, OUTPUT, FILTER)

%type <i> idval

CF_GRAMMAR

/* Setting of router ID */

CF_ADDTO(conf, rtrid)
rtrid: ROUTER ID idval {
   new_config->router_id = $3;
   }
 ;

idval:
   NUM
 | IPA { $$ = ipa_to_u32($1); }
 ;

/* Definition of protocols */

CF_ADDTO(conf, proto)

proto_start: PROTOCOL

proto_name:
   /* EMPTY */ {
     struct symbol *s = cf_default_name(this_proto->proto->name);
     s->class = SYM_PROTO;
     s->def = this_proto;
     this_proto->name = s->name;
     }
 | SYM {
     if ($1->class) cf_error("Symbol already defined");
     $1->class = SYM_PROTO;
     $1->def = this_proto;
     this_proto->name = $1->name;
   }
 ;

proto_item:
   /* EMPTY */
 | PREFERENCE expr {
     if ($2 < 0 || $2 > 255) cf_error("Invalid preference");
     this_proto->preference = $2;
   }
 | DISABLED { this_proto->disabled = 1; }
 | DEBUG expr { this_proto->debug = $2; }
 | DEBUG ALL { this_proto->debug = ~0; }
 | DEBUG OFF { this_proto->debug = 0; }
 | INPUT FILTER filter { this_proto->in_filter = $3; }
 | OUTPUT FILTER filter { this_proto->out_filter = $3; }
 ;

/* Device protocol */

CF_ADDTO(proto, dev_proto '}')

dev_proto_start: proto_start DEVICE {
     if (!(this_proto = cf_dev_proto)) cf_error("Device protocol already defined");
     cf_dev_proto = NULL;
   }
 ;

dev_proto:
   dev_proto_start '{'
 | dev_proto proto_item ';'
 | dev_proto dev_iface_list ';'
 ;

dev_iface_list:
   INTERFACE TEXT {
     init_list(&((struct rt_dev_config *) this_proto)->iface_list);
     rt_dev_add_iface($2);
   }
 | dev_iface_list ',' TEXT { rt_dev_add_iface($3); }
 ;

CF_CODE

void
rt_dev_add_iface(char *n)
{
  struct rt_dev_config *p = (void *) this_proto;
  struct iface_patt *k = cfg_alloc(sizeof(struct iface_patt));

  k->pattern = cfg_strdup(n);
  add_tail(&p->iface_list, &k->n);
}

CF_END
