/*
 *	Rest in pieces - RIP protocol
 *
 *	(c) 1998 Pavel Machek <pavel@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include <string.h>
#include <stdlib.h>

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/protocol.h"
#include "nest/route.h"
#include "lib/socket.h"
#include "lib/resource.h"
#include "lib/lists.h"
#include "lib/timer.h"

#include "rip.h"

int infinity = 16;

/* XXX should be 520 */
#define RIP_PORT 1520

/* XXX should be 30 */
#define RIP_TIME 5

static void
rip_reply(struct proto *p)
{
  P->listen->tbuf = "ACK!";
  sk_send_to( P->listen, 5, P->listen->faddr, P->listen->fport );
}

/*
 * Entries
 */

static struct rip_entry *
find_entry( struct proto *p, ip_addr network, ip_addr netmask )
{
  struct node *e;

  CHK_MAGIC;
  WALK_LIST( e, P->rtable ) {
    if (ipa_equal( network, E->network ) &&
	(ipa_mklen( netmask ) == E->pxlen)) {
      return E;
    }
  }
  return NULL;
}

static struct rip_entry *
new_entry( struct proto *p, struct rip_block *b, ip_addr whotoldme )
{
  struct rip_entry *e;

  e = mb_alloc(p->pool, sizeof( struct rip_entry ));
  e->whotoldme = whotoldme;
  e->network = b->network;
  e->pxlen = ipa_mklen( b->netmask );
  /* FIXME: verify that it is really netmask */
  if (ipa_nonzero(b->nexthop))
    e->nexthop = b->nexthop;
  else
    e->nexthop = whotoldme;
  e->tag = ntohs( b->tag );
  e->metric = ntohl( b->metric );
  e->updated = e->changed = now;

  {
    rta *a, A;
    rte *r;
    net *n;
    
    bzero(&A, sizeof(A));
    A.proto = p;
    A.source = RTS_RIP;
    A.scope = SCOPE_UNIVERSE;
    A.cast = RTC_UNICAST;
    A.dest = RTD_ROUTER;
    A.tos = 0;
    A.flags = 0;
    A.gw = e->nexthop;
    A.from = e->whotoldme;
    A.iface = /* fixme: need to set somehow */ NULL;
    /* set to: interface of nexthop */
    a = rta_lookup(&A);
    n = net_get( &master_table, 0, e->network, e->pxlen );
    r = rte_get_temp(a);
    r->pflags = 0; /* Here go my flags */
    rte_update( n, p, r );
  }
  
  return e;
}

static void
kill_entry( struct proto *p, struct rip_entry *e )
{
  struct rip_connection *c;
  net *n;

  n = net_find(&master_table, 0, e->network, e->pxlen );
  if (!n) debug( "Could not find entry to delete in main routing table.\n" );
     else rte_update( n, p, NULL );

  rem_node( NODE e );
  WALK_LIST( c, P->connections ) {
    if (c->sendptr == e)
      die( "kill_entry: one of connections has this as current, fix me!" );
  }
  mb_free( e );
}

/*
 * Output processing
 */

static void
rip_tx_err( sock *s, int err )
{
  struct rip_connection *c = s->data;
  struct proto *p = c->proto;
  log( L_ERR "Unexpected error at rip transmit\n" );
}

static void
rip_tx( sock *s )
{
  struct rip_connection *c = s->data;
  struct proto *p = c->proto;
  struct rip_packet *packet = (void *) s->tbuf;
  int i, done = 0;

givemore:

  debug( "Preparing packet to send: " );

  if (!(NODE c->sendptr)->next) {
    debug( "Looks like I'm done\n" );
    return;
  }

  packet->heading.command = 99; /* FIXME */
  packet->heading.version = RIP_V2;
  packet->heading.unused = 0;

  for (i = 0; i < 25; i++) {
    if (!(NODE c->sendptr)->next)
      break;
    debug( "." );
    packet->block[i].family  = htons( 0 ); /* Is it right? */
    packet->block[i].tag	    = htons( 0 ); /* What should I set it to? */
    packet->block[i].network = c->sendptr->network;
    packet->block[i].netmask = ipa_mkmask( c->sendptr->pxlen );
    packet->block[i].nexthop = IPA_NONE; /* FIXME: How should I set it? */
    packet->block[i].metric  = htonl( c->sendptr->metric );
    ipa_hton( packet->block[i].network );
    ipa_hton( packet->block[i].netmask );
    ipa_hton( packet->block[i].nexthop );

    c->sendptr = (void *) (NODE c->sendptr)->next;
  }

  debug( ", sending %d blocks, ", i+1 );

  i = sk_send( s, sizeof( struct rip_packet_heading ) + (i+1)*sizeof( struct rip_block ) );
  if (i<0) rip_tx_err( s, i );
  if (i>0) {
    debug( "it wants more\n" );
    goto givemore;
  }
  
  debug( "blocked\n" );
}

static void
rip_sendto( struct proto *p, ip_addr daddr, int dport )
{
  struct rip_connection *c = mb_alloc( p->pool, sizeof( struct rip_connection ));
  static int num = 0;

  c->addr = daddr;
  c->proto = p;
  c->num = num++;

  c->send = sk_new( p->pool );
  c->send->type = SK_UDP;
  c->send->sport = RIP_PORT;
  c->send->dport = dport;
  c->send->daddr = daddr;
  c->send->rx_hook = NULL;
  c->send->tx_hook = rip_tx;
  c->send->err_hook = rip_tx_err;
  c->send->data = c;
  c->send->tbuf = mb_alloc( p->pool, sizeof( struct rip_packet ));
  if (sk_open(c->send)<0) {
    log( L_ERR "Could not send data to %I:%d\n", daddr, dport );
    return;
  }

  c->sendptr = HEAD( P->rtable );  
  add_head( &P->connections, NODE c );
  debug( "Sending my routing table to %I:%d\n", daddr, dport );

  rip_tx( c->send );
}


/*
 * Input processing
 */

static int
process_authentication( struct proto *p, struct rip_block *block )
{
  return 0;
}

#define DELETE( msg ) { debug( msg ); kill_entry( p, e ); e = NULL; }

static void
process_block( struct proto *p, struct rip_block *block, ip_addr whotoldme )
{
  struct rip_entry *e;
  int metric = ntohl( block->metric );
  ip_addr network = block->network;

  CHK_MAGIC;
  if ((!metric) || (metric > infinity)) {
    log( L_WARN "Got metric %d from %I\n", metric, whotoldme );
    return;
  }

  /* XXX Check if destination looks valid - ie not net 0 or 127 */

  /* XXX Should add configurable ammount */
  if (metric < infinity)
    metric++;

  debug( "block: %I tells me: %I/%I available, metric %d... ", whotoldme, network, block->netmask, metric );

  e = find_entry( p, network, block->netmask );
  if (e && (e->metric > metric)) /* || if same metrics and this is much newer */ 
    DELETE( "better metrics... " );

  if (e && ((e->updated - now) > 180))
    DELETE( "more than 180 seconds old... " );

  if (e && ipa_equal( whotoldme, e->whotoldme ) &&
      (e->metric == metric)) {
    debug( "old news, updating time" );
    e->updated = now;
  }

  if (!e) {
    debug( "this is new" );
    e = new_entry( p, block, whotoldme );
    add_head( &P->rtable, NODE e );
  }

  debug( "\n" );
}
#undef DELETE

#define BAD( x ) { log( L_WARN "RIP/%s: " x "\n", p->name ); return 1; }

static int
rip_process_packet( struct proto *p, struct rip_packet *packet, int num, ip_addr whotoldme, int port )
{
  int i;
  int native_class = 0;

  switch( packet->heading.version ) {
  case RIP_V1: debug( "Rip1: " ); break;
  case RIP_V2: debug( "Rip2: " ); break;
  default: BAD( "Unknown version" );
  }

  switch( packet->heading.command ) {
  case 1: debug( "Asked to send my routing table\n" ); 
    	  rip_sendto( p, whotoldme, port );
          break;
  case 2: debug( "Part of routing table came\n" ); 
          if (port != RIP_PORT) {
	    log( L_AUTH "%I send me routing info from port %d\n", whotoldme, port );
	    return 0;
	  }

	  if (!neigh_find( p, &whotoldme, 0 )) {
	    log( L_AUTH "%I send me routing info but he is not my neighbour\n", whotoldme );
	    return 0;
	  }

	  /* Should check if it is not my own packet */

          for (i=0; i<num; i++) {
	    struct rip_block *block = &packet->block[i];
	    if (block->family == 0xffff)
	      if (!i) {
		if (process_authentication(p, block))
		  BAD( "Authentication failed\n" );
	      } else BAD( "Authentication is not the first!" );
	    ipa_ntoh( block->network );
	    ipa_ntoh( block->netmask );
	    ipa_ntoh( block->nexthop );
	    if (packet->heading.version == RIP_V1) {
	      block->netmask = block->network; /* MJ: why are macros like this?! */
	      ipa_class_mask( block->netmask );
	    }
	    process_block( p, block, whotoldme );
	  }
          break;
  case 3:
  case 4: BAD( "I was asked for traceon/traceoff\n" );
  case 5: BAD( "Some Sun extension around here\n" );
  default: BAD( "Unknown command" );
  }

  rip_reply(p);
  return 0;
}

static int
rip_rx(sock *s, int size)
{
  struct proto *p = s->data;
  int num;

  CHK_MAGIC;
  debug( "RIP: message came: %d bytes\n", size );
  if (size <= 0) BAD( "Zero sized packet" );
  size -= sizeof( struct rip_packet_heading );
  if (size < 0) BAD( "Too small packet" );
  if (size % sizeof( struct rip_block )) BAD( "Odd sized packet" );
  num = size / sizeof( struct rip_block );
  if (num>25) BAD( "Too many blocks" );

  rip_process_packet( p, (struct rip_packet *) s->rbuf, num, s->faddr, s->fport );
  return 1;
}

/*
 * Interface to rest of bird
 */

static void
rip_dump_entry( struct rip_entry *e )
{
  debug( "%I told me %d/%d ago: to %I/%d go via %I, metric %d\n", 
  e->whotoldme, e->updated-now, e->changed-now, e->network, e->pxlen, e->nexthop, e->metric );
}

static void
rip_timer(timer *t)
{
  struct proto *p = t->data;
  struct rip_entry *e, *et;

  CHK_MAGIC;
  debug( "RIP: tick tock\n" );

  WALK_LIST_DELSAFE( e, et, P->rtable ) {
    if ((now - E->updated) > (180+120)) {
      debug( "RIP: entry is too old: " );
      rip_dump_entry( E );
      kill_entry( p, E );
    }
  }

  debug( "RIP: Broadcasting routing tables\n" );
  rip_sendto( p, _MI( 0x0a000001 ), RIP_PORT );

  debug( "RIP: tick tock done\n" );
}

static void
rip_start(struct proto *p)
{
  debug( "RIP: initializing instance...\n" );

  P->listen = sk_new( p->pool );
  P->listen->type = SK_UDP;
  P->listen->sport = RIP_PORT;
  P->listen->rx_hook = rip_rx;
  P->listen->data = p;
  P->listen->rbsize = 10240;
  P->magic = RIP_MAGIC;
  if (sk_open(P->listen)<0)
    die( "RIP/%s: could not listen\n", p->name );
  init_list( &P->rtable );
  init_list( &P->connections );
  P->timer = tm_new( p->pool );
  P->timer->data = p;
  P->timer->randomize = 5;
  P->timer->recurrent = RIP_TIME; 
  P->timer->hook = rip_timer;
  tm_start( P->timer, 5 );

  debug( "RIP: ...done\n");
}

static void
rip_init(struct protocol *p)
{
  debug( "RIP: initializing RIP...\n" );
}

static void
rip_dump(struct proto *p)
{
  int i;
  node *w, *e;
  i = 0;
  WALK_LIST( w, P->connections ) {
    struct rip_connection *n = (void *) w;
    debug( "RIP: connection #%d: %I\n", n->num, n->addr );
  }
  i = 0;
  WALK_LIST( e, P->rtable ) {
    debug( "RIP: entry #%d: ", i++ );
    rip_dump_entry( E );
  }
}

static void
rip_if_notify(struct proto *p, unsigned c, struct iface *old, struct iface *new)
{
}

static void
rip_preconfig(struct protocol *x)
{
  struct proto *p = proto_new(&proto_rip, sizeof(struct rip_data));

  debug( "RIP: preconfig\n" );
  p->preference = DEF_PREF_DIRECT;
  p->start = rip_start;
  p->if_notify = rip_if_notify;
  p->dump = rip_dump;
}

static void
rip_postconfig(struct protocol *p)
{
}

struct protocol proto_rip = {
  { NULL, NULL },
  "RIP",
  0,
  rip_init,
  rip_preconfig,
  rip_postconfig
};

