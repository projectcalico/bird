/*
 *	BIRD -- Core Configuration
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

static struct proto_config *this_proto;
static struct iface_patt *this_ipatt;

#include "nest/rt-dev.h"
#include "nest/password.h"

CF_DECLS

CF_KEYWORDS(ROUTER, ID, PROTOCOL, PREFERENCE, DISABLED, DEBUG, ALL, OFF, DIRECT)
CF_KEYWORDS(INTERFACE, IMPORT, EXPORT, FILTER, NONE, TABLE)
CF_KEYWORDS(PASSWORD, FROM, PASSIVE, TO, ID)

CF_ENUM(T_ENUM_RTS, RTS_, DUMMY, STATIC, INHERIT, DEVICE, STATIC_DEVICE, REDIRECT,
	RIP, RIP_EXT, OSPF, OSPF_EXT, OSPF_IA, OSPF_BOUNDARY, BGP, PIPE)

%type <i32> idval
%type <f> imexport
%type <r> rtable
%type <p> password_list password_begin
%type <s> optsym
%type <ra> r_args

CF_GRAMMAR

/* Setting of router ID */

CF_ADDTO(conf, rtrid)

rtrid: ROUTER ID idval ';' {
   new_config->router_id = $3;
   }
 ;

idval:
   NUM { $$ = $1; }
 | RTRID
 | IPA {
#ifndef IPV6
     $$ = ipa_to_u32($1);
#else
     cf_error("Router IDs must be entered as hexadecimal numbers in IPv6 version");
#endif
   }
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
     struct symbol *s = cf_default_name(this_proto->protocol->name, &this_proto->protocol->name_counter);
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

/* Interface patterns */

iface_patt:
   TEXT { this_ipatt->pattern = $1; this_ipatt->prefix = IPA_NONE; this_ipatt->pxlen = 0; }
 | IPA pxlen { this_ipatt->pattern = NULL; this_ipatt->prefix = $1; this_ipatt->pxlen = $2; }
 | TEXT IPA pxlen { this_ipatt->pattern = $1; this_ipatt->prefix = $2; this_ipatt->pxlen = $3; }
 ;

/* Direct device route protocol */

CF_ADDTO(proto, dev_proto '}')

dev_proto_start: proto_start DIRECT {
     struct rt_dev_config *p = proto_config_new(&proto_device, sizeof(struct rt_dev_config));
     this_proto = &p->c;
     p->c.preference = DEF_PREF_DIRECT;
     init_list(&p->iface_list);
   }
 ;

dev_proto:
   dev_proto_start proto_name '{'
 | dev_proto proto_item ';'
 | dev_proto dev_iface_list ';'
 ;

dev_iface_entry_init:
   /* EMPTY */ {
     struct rt_dev_config *p = (void *) this_proto;
     struct iface_patt *k = cfg_allocz(sizeof(struct iface_patt));
     add_tail(&p->iface_list, &k->n);
     this_ipatt = k;
   }
 ;

dev_iface_entry:
   dev_iface_entry_init iface_patt
 ;

dev_iface_list:
   INTERFACE dev_iface_entry
 | dev_iface_list ',' dev_iface_entry
 ;

/* Password lists */

password_begin: 
   PASSWORD TEXT {
     last_password_item = cfg_alloc(sizeof (struct password_item));
     last_password_item->password = $2;
     last_password_item->from = 0;
     last_password_item->to = TIME_INFINITY;
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
     $1->next = $4;
     $$ = $1;
   }
 ;

/* Core commands */

CF_CLI_HELP(SHOW,,[[Show status information]])

CF_CLI(SHOW STATUS,,, [[Show router status]]) {
  cli_msg(1000, "BIRD " BIRD_VERSION);
  /* FIXME: Should include uptime, shutdown flag et cetera */
} ;

CF_CLI(SHOW PROTOCOLS, optsym, [<name>], [[Show routing protocols]])
{ proto_show($3, 0); } ;

CF_CLI(SHOW PROTOCOLS VERBOSE, optsym, [<name>], [[Show routing protocol details]])
{ proto_show($4, 1); } ;

optsym:
   SYM
 | /* empty */ { $$ = NULL; }
 ;

CF_CLI(SHOW INTERFACES,,, [[Show network interfaces]])
{ if_show(); } ;

CF_CLI(SHOW INTERFACES SUMMARY,,, [[Show summary of network interfaces]])
{ if_show_summary(); } ;

CF_CLI(SHOW ROUTE, r_args, [<prefix>] [table <t>] [filter <f>] [all], [[Show routing table]])
{ rt_show($3); } ;

r_args:
   /* empty */ {
     $$ = cfg_allocz(sizeof(struct rt_show_data));
     $$->pxlen = 256;
     $$->filter = FILTER_ACCEPT;
     $$->table = config->master_rtc->table;
   }
 | r_args IPA pxlen {
     $$ = $1;
     if ($$->pxlen != 256) cf_error("Only one prefix expected");
     if (!ip_is_prefix($2, $3)) cf_error("Invalid prefix");
     $$->prefix = $2;
     $$->pxlen = $3;
   }
 | r_args TABLE SYM {
     $$ = $1;
     if ($3->class != SYM_TABLE) cf_error("%s is not a table", $3->name);
     $$->table = ((struct rtable_config *)$3->def)->table;
   }
 | r_args FILTER filter {
     $$ = $1;
     if ($$->filter != FILTER_ACCEPT) cf_error("Filter specified twice");
     $$->filter = $3;
   }
 | r_args where_filter {
     $$ = $1;
     if ($$->filter != FILTER_ACCEPT) cf_error("Filter specified twice");
     $$->filter = $2;
   }
 | r_args ALL {
     $$ = $1;
     $$->verbose = 1;
   }
 ;

/* FIXME: These are examples. Remove them soon. */
CF_CLI_HELP(TEST, <subsystem>, [[Tests different subsystems]])
CF_CLI(TEST LEDS, NUM, <N>, [[Flash each LED <N> times]]) { cli_msg(0, "%d", $3); } ;
CF_CLI(TEST MEMORY,,, [[Replace all useful information by testing patterns]]) { cli_msg(0, "DONE"); } ;
CF_CLI(TEST LONG,,, [[Test long replies]]) {
	static void test_command(struct cli *);
	this_cli->cont = test_command;
	this_cli->rover = (void *) 1;
	cli_msg(-2, "Start");
} ;

CF_CODE

/* FIXME: Test only, remove */
static void test_command(struct cli *c)
{
	int i = (int) c->rover;
	if (i < 10) {
		cli_printf(c, -3, "%d", i);
		c->rover = (void *) ++i;
	} else {
		c->cont = NULL;
		cli_printf(c, 4, "DONE");
	}
}

CF_END
