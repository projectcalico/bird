/*
 *	BIRD -- Core Configuration
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

static struct proto *this_proto;

CF_DECLS

CF_KEYWORDS(ROUTER, ID, PROTOCOL, PREFERENCE)

%type <i> idval

CF_GRAMMAR

/* Setting of router ID */

CF_ADDTO(conf, rtrid)
rtrid: ROUTER ID idval {
   router_id = $3;
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
die("pref=%d", $2);
   }
 ;

CF_CODE

CF_END
