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
#include "nest/password.h"

void rt_dev_add_iface(char *);

CF_DECLS

CF_KEYWORDS(ROUTER, ID, PROTOCOL, PREFERENCE, DISABLED, DEBUG, ALL, OFF, DIRECT)
CF_KEYWORDS(INTERFACE, IMPORT, EXPORT, FILTER, NONE, TABLE)
CF_KEYWORDS(PASSWORD, FROM, PASSIVE, TO, ID)

%type <i> idval
%type <f> imexport
%type <r> rtable
%type <p> password_list password_begin

CF_GRAMMAR

/* Setting of router ID */

CF_ADDTO(conf, rtrid)

rtrid: ROUTER ID idval ';' {
   new_config->router_id = $3;
   }
 ;

idval:
   NUM
 | IPA { $$ = ipa_to_u32($1); }
 ;

/* Creation of routing tables */

CF_ADDTO(conf, newtab)

newtab: TABLE SYM {
     struct rtable_config *c = cfg_allocz(sizeof(struct rtable_config));
     struct symbol *s = $2;
     cf_define_symbol(s, SYM_TABLE, c);
     c->name = s->name;
     add_tail(&new_config->tables, &c->n);
   }
 ;

/* Definition of protocols */

CF_ADDTO(conf, proto)

proto_start: PROTOCOL

proto_name:
   /* EMPTY */ {
     struct symbol *s = cf_default_name(this_proto->proto->name, &this_proto->proto->name_counter);
     s->class = SYM_PROTO;
     s->def = this_proto;
     this_proto->name = s->name;
     }
 | SYM {
     cf_define_symbol($1, SYM_PROTO, this_proto);
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
 | IMPORT imexport { this_proto->in_filter = $2; }
 | EXPORT imexport { this_proto->out_filter = $2; }
 | TABLE rtable { this_proto->table = $2; }
 ;

imexport:
   FILTER filter { $$ = $2; }
 | ALL { $$ = FILTER_ACCEPT; }
 | NONE { $$ = FILTER_REJECT; }
 ;

rtable:
   SYM {
     if ($1->class != SYM_TABLE) cf_error("Table name expected");
     $$ = $1->def;
   }
 ;

/* Direct device route protocol */

CF_ADDTO(proto, dev_proto '}')

dev_proto_start: proto_start DIRECT {
     struct rt_dev_config *p = proto_config_new(&proto_device, sizeof(struct rt_dev_config));
     struct iface_patt *k = cfg_alloc(sizeof(struct iface_patt));
     this_proto = &p->c;
     p->c.preference = DEF_PREF_DIRECT;
     init_list(&p->iface_list);
     k->pattern = "*";
     add_tail(&p->iface_list, &k->n);
   }
 ;

dev_proto:
   dev_proto_start proto_name '{'
 | dev_proto proto_item ';'
 | dev_proto dev_iface_list ';'
 ;

dev_iface_list:
   INTERFACE TEXT {
     /* FIXME: Beware of obscure semantics. */
     init_list(&((struct rt_dev_config *) this_proto)->iface_list);
     rt_dev_add_iface($2);
   }
 | dev_iface_list ',' TEXT { rt_dev_add_iface($3); }
 ;

password_begin: 
   PASSWORD TEXT {
     last_password_item = cfg_alloc(sizeof (struct password_item));
     last_password_item->password = $2;
     last_password_item->from = 0;
     last_password_item->to = ~0;
     last_password_item->id = 0;
     last_password_item->next = NULL;
     $$=last_password_item;
   }
 ;

password_items:
   /* empty */ { } 
 | FROM datetime password_items { last_password_item->from = $2; }
 | TO datetime password_items { last_password_item->to = $2; }
 | PASSIVE datetime password_items { last_password_item->passive = $2; }
 | ID NUM password_items { last_password_item->id = $2; }
 ;

password_list: 
   /* empty */ { $$ = NULL; } 
 | password_begin password_items ';' password_list {
     last_password_item->next = $4;
     $$ = last_password_item;
   }
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
