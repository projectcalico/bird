/*
 *	BIRD -- Core Configuration
 *
 *	(c) 1998--2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

CF_HDR

#include "nest/rt-dev.h"
#include "nest/password.h"
#include "nest/cmds.h"

CF_DEFINES

static struct proto_config *this_proto;
static struct iface_patt *this_ipatt;

CF_DECLS

CF_KEYWORDS(ROUTER, ID, PROTOCOL, PREFERENCE, DISABLED, DEBUG, ALL, OFF, DIRECT)
CF_KEYWORDS(INTERFACE, IMPORT, EXPORT, FILTER, NONE, TABLE, STATES, ROUTES, FILTERS)
CF_KEYWORDS(PASSWORD, FROM, PASSIVE, TO, ID, EVENTS, PACKETS, PROTOCOLS, INTERFACES)
CF_KEYWORDS(PRIMARY, STATS, COUNT, FOR, COMMANDS)

CF_ENUM(T_ENUM_RTS, RTS_, DUMMY, STATIC, INHERIT, DEVICE, STATIC_DEVICE, REDIRECT,
	RIP, OSPF, OSPF_EXT, OSPF_IA, OSPF_BOUNDARY, BGP, PIPE)
CF_ENUM(T_ENUM_SCOPE, SCOPE_, HOST, LINK, SITE, ORGANIZATION, UNIVERSE)
CF_ENUM(T_ENUM_RTC, RTC_, UNICAST, BROADCAST, MULTICAST, ANYCAST)
CF_ENUM(T_ENUM_RTD, RTD_, ROUTER, DEVICE, BLACKHOLE, UNREACHABLE, PROHIBIT)

%type <i32> idval
%type <f> imexport
%type <r> rtable
%type <p> password_list password_begin
%type <s> optsym
%type <ra> r_args
%type <i> echo_mask echo_size debug_mask debug_list debug_flag import_or_proto
%type <t> proto_patt

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
     cf_error("Router IDs must be entered as hexadecimal numbers or IPv4 addresses in IPv6 version");
#endif
   }
 ;

/* Creation of routing tables */

CF_ADDTO(conf, newtab)

newtab: TABLE SYM {
   rt_new_table($2);
   }
 ;

/* Definition of protocols */

CF_ADDTO(conf, proto)

proto_start: PROTOCOL

proto_name:
   /* EMPTY */ {
     struct symbol *s = cf_default_name(this_proto->protocol->template, &this_proto->protocol->name_counter);
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
 | DEBUG debug_mask { this_proto->debug = $2; }
 | IMPORT imexport { this_proto->in_filter = $2; }
 | EXPORT imexport { this_proto->out_filter = $2; }
 | TABLE rtable { this_proto->table = $2; }
 ;

imexport:
   FILTER filter { $$ = $2; }
 | where_filter
 | ALL { $$ = FILTER_ACCEPT; }
 | NONE { $$ = FILTER_REJECT; }
 ;

rtable:
   SYM {
     if ($1->class != SYM_TABLE) cf_error("Table name expected");
     $$ = $1->def;
   }
 ;

CF_ADDTO(conf, debug_default)

debug_default:
   DEBUG PROTOCOLS debug_mask { new_config->proto_default_debug = $3; }
 | DEBUG COMMANDS expr { new_config->cli_debug = $3; }
 ;

/* Interface patterns */

iface_patt:
   TEXT { this_ipatt->pattern = $1; this_ipatt->prefix = IPA_NONE; this_ipatt->pxlen = 0; }
 | prefix { this_ipatt->pattern = NULL; this_ipatt->prefix = $1.addr; this_ipatt->pxlen = $1.len; }
 | TEXT prefix { this_ipatt->pattern = $1; this_ipatt->prefix = $2.addr; this_ipatt->pxlen = $2.len; }
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

/* Debug flags */

debug_mask:
   ALL { $$ = ~0; }
 | OFF { $$ = 0; }
 | '{' debug_list '}' { $$ = $2; }
 ;

debug_list:
   debug_flag
 | debug_list ',' debug_flag { $$ = $1 | $3; }
 ;

debug_flag:
   STATES	{ $$ = D_STATES; }
 | ROUTES	{ $$ = D_ROUTES; }
 | FILTERS	{ $$ = D_FILTERS; }
 | INTERFACES	{ $$ = D_IFACES; }
 | EVENTS	{ $$ = D_EVENTS; }
 | PACKETS	{ $$ = D_PACKETS; }
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
 | ID expr password_items { last_password_item->id = $2; }
 ;

password_list: 
   /* empty */ { $$ = NULL; } 
 | password_begin password_items ';' password_list {
     $1->next = $4;
     $$ = $1;
   }
 ;

/* Core commands */

CF_CLI_HELP(SHOW, ..., [[Show status information]])

CF_CLI(SHOW STATUS,,, [[Show router status]])
{ cmd_show_status(); }

CF_CLI(SHOW PROTOCOLS, optsym, [<name>], [[Show routing protocols]])
{ proto_show($3, 0); } ;

CF_CLI(SHOW PROTOCOLS ALL, optsym, [<name>], [[Show routing protocol details]])
{ proto_show($4, 1); } ;

optsym:
   SYM
 | /* empty */ { $$ = NULL; }
 ;

CF_CLI(SHOW INTERFACES,,, [[Show network interfaces]])
{ if_show(); } ;

CF_CLI(SHOW INTERFACES SUMMARY,,, [[Show summary of network interfaces]])
{ if_show_summary(); } ;

CF_CLI(SHOW ROUTE, r_args, [<prefix>] [table <t>] [filter <f>] [all] [primary] [(import|protocol) <p>] [stats], [[Show routing table]])
{ rt_show($3); } ;

r_args:
   /* empty */ {
     $$ = cfg_allocz(sizeof(struct rt_show_data));
     $$->pxlen = 256;
     $$->filter = FILTER_ACCEPT;
     $$->table = config->master_rtc->table;
   }
 | r_args prefix {
     $$ = $1;
     if ($$->pxlen != 256) cf_error("Only one prefix expected");
     $$->prefix = $2.addr;
     $$->pxlen = $2.len;
   }
 | r_args FOR prefix_or_ipa {
     $$ = $1;
     if ($$->pxlen != 256) cf_error("Only one prefix expected");
     $$->prefix = $3.addr;
     $$->pxlen = $3.len;
     $$->show_for = 1;
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
 | r_args PRIMARY {
     $$ = $1;
     $$->primary_only = 1;
   }
 | r_args import_or_proto SYM {
     struct proto_config *c = (struct proto_config *) $3->def;
     $$ = $1;
     if ($$->import_mode) cf_error("Protocol specified twice");
     if ($3->class != SYM_PROTO || !c->proto) cf_error("%s is not a protocol", $3->name);
     $$->import_mode = $2;
     $$->primary_only = 1;
     $$->import_protocol = c->proto;
     $$->running_on_config = c->proto->cf->global;
   }
 | r_args STATS {
     $$ = $1;
     $$->stats = 1;
   }
 | r_args COUNT {
     $$ = $1;
     $$->stats = 2;
   }
 ;

import_or_proto:
   IMPORT { $$ = 1; }
 | PROTOCOL { $$ = 2; }
 ;

CF_CLI(SHOW SYMBOLS, optsym, [<symbol>], [[Show all known symbolic names]])
{ cmd_show_symbols($3); } ;

CF_CLI_HELP(DUMP, ..., [[Dump debugging information]])
CF_CLI(DUMP RESOURCES,,, [[Dump all allocated resource]])
{ rdump(&root_pool); cli_msg(0, ""); } ;
CF_CLI(DUMP SOCKETS,,, [[Dump open sockets]])
{ sk_dump_all(); cli_msg(0, ""); } ;
CF_CLI(DUMP INTERFACES,,, [[Dump interface information]])
{ if_dump_all(); cli_msg(0, ""); } ;
CF_CLI(DUMP NEIGHBORS,,, [[Dump neighbor cache]])
{ neigh_dump_all(); cli_msg(0, ""); } ;
CF_CLI(DUMP ATTRIBUTES,,, [[Dump attribute cache]])
{ rta_dump_all(); cli_msg(0, ""); } ;
CF_CLI(DUMP ROUTES,,, [[Dump routing table]])
{ rt_dump_all(); cli_msg(0, ""); } ;
CF_CLI(DUMP PROTOCOLS,,, [[Dump protocol information]])
{ protos_dump_all(); cli_msg(0, ""); } ;

CF_CLI(ECHO, echo_mask echo_size, [all | off | <mask>] [<buffer-size>], [[Configure echoing of log messages]]) {
  cli_set_log_echo(this_cli, $2, $3);
  cli_msg(0, "");
} ;

echo_mask:
   ALL { $$ = ~0; }
 | OFF { $$ = 0; }
 | NUM
 ;

echo_size:
   /* empty */ { $$ = 4096; }
 | NUM {
     if ($1 < 256 || $1 > 65536) cf_error("Invalid log buffer size");
     $$ = $1;
   }
 ;

CF_CLI(DISABLE, proto_patt, <protocol> | <pattern> | all, [[Disable protocol]])
{ proto_xxable($2, 0); } ;
CF_CLI(ENABLE, proto_patt, <protocol> | <pattern> | all, [[Enable protocol]])
{ proto_xxable($2, 1); } ;
CF_CLI(RESTART, proto_patt, <protocol> | <pattern> | all, [[Restart protocol]])
{ proto_xxable($2, 2); } ;

CF_CLI_HELP(DEBUG, ..., [[Control protocol debugging]])
CF_CLI(DEBUG, proto_patt debug_mask, (<protocol> | <pattern> | all) (all | off | { states | routes | filters | events | packets }), [[Control protocol debugging]])
{ proto_debug($2, $3); }

proto_patt:
   SYM { $$ = $1->name; }
 | ALL { $$ = "*"; }
 | TEXT
 ;

CF_CODE

CF_END
