/*
 *	Rest in pieces - RIP protocol
 *
 *	Copyright (c) 1998 Pavel Machek <pavel@ucw.cz>
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

/* FIXME: should be 520 */
#define RIP_PORT 1520

/* FIXME: should be 30 */
#define RIP_TIME 5

/* FIXME: should be 120+180 */
#define RIP_GB_TIME 30

static void
rip_reply(struct proto *p)
{
#if 0
  P->listen->tbuf = "ACK!";
  sk_send_to( P->listen, 5, P->listen->faddr, P->listen->fport );
#endif
}

/*
 * Entries
 */

static struct rip_entry *
new_entry( struct proto *p )
{
  struct rip_entry *e;
  e = mb_alloc(p->pool, sizeof( struct rip_entry ));
  bzero( e, sizeof( struct rip_entry ));
  return e;
}

static struct rip_entry *
find_entry( struct proto *p, ip_addr network, int pxlen )
{
  struct node *e;

  CHK_MAGIC;
  WALK_LIST( e, P->rtable ) {
    if (ipa_equal( network, E->network ) &&
	(pxlen == E->pxlen)) {
      return E;
    }
  }
  return NULL;
}

/* Delete one of entries */
static void
kill_entry_ourrt( struct proto *p, struct rip_entry *e )
{
  struct rip_connection *c;
  net *n;

  rem_node( NODE e );
  WALK_LIST( c, P->connections ) {
    if (c->sendptr == e) {
      debug( "Deleting from under someone's sendptr...\n" );
      c->sendptr = (void *) (NODE c->sendptr)->next;
    }
  }
  mb_free( e );
}

#if 0
/* Currently unreferenced, and likely may stay that way */ 
/* Delete one of entries */
static void
kill_entry_mainrt( struct proto *p, struct rip_entry *e )
{
  struct rip_connection *c;
  net *n;

  n = net_find(&master_table, 0, e->network, e->pxlen );
  if (!n) log( L_ERR "Could not find entry to delete in main routing table.\n" );
     else rte_update( n, p, NULL );
}

static void
kill_entry( struct proto *p, struct rip_entry *e )
{
  kill_entry_mainrt( p, e );
  kill_entry_ourrt( p, e );
}
#endif

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
  struct rip_interface *rif = s->data;
  struct rip_connection *c = rif->busy;
  struct proto *p = c->proto;
  struct rip_packet *packet = (void *) s->tbuf;
  int i, done = 0;

givemore:

  debug( "Preparing packet to send: " );

  if (!(NODE c->sendptr)->next) {
    debug( "Looks like I'm" );
    c->rif->busy = NULL;
    rem_node(NODE c);
    mb_free(c);
    debug( " done\n" );
    return;
  }

  packet->heading.command = RIPCMD_RESPONSE;
  packet->heading.version = RIP_V2;
  packet->heading.unused  = 0;

  for (i = 0; i < 25; i++) {
    if (!(NODE c->sendptr)->next)
      break;
    debug( "." );
    packet->block[i].family  = htons( 2 ); /* AF_INET */
    packet->block[i].tag     = htons( 0 ); /* FIXME: What should I set it to? */
    packet->block[i].network = c->sendptr->network;
    packet->block[i].netmask = ipa_mkmask( c->sendptr->pxlen );
    packet->block[i].nexthop = IPA_NONE; /* FIXME: How should I set it? */
    packet->block[i].metric  = htonl( 1+ (c->sendptr->metric?:1) );
    if (ipa_equal(c->sendptr->whotoldme, s->daddr)) {
      debug( "(split horizont)" );
      /* FIXME: should we do it in all cases? */
      packet->block[i].metric = P->infinity;
    }
    ipa_hton( packet->block[i].network );
    ipa_hton( packet->block[i].netmask );
    ipa_hton( packet->block[i].nexthop );

    c->sendptr = (void *) (NODE c->sendptr)->next;
  }

  debug( ", sending %d blocks, ", i );

  if (ipa_nonzero(c->daddr))
    i = sk_send_to( s, sizeof( struct rip_packet_heading ) + i*sizeof( struct rip_block ), c->daddr, c->dport );
  else
    i = sk_send( s, sizeof( struct rip_packet_heading ) + i*sizeof( struct rip_block ) );
  if (i<0) rip_tx_err( s, i );
  if (i>0) {
    debug( "it wants more\n" );
    goto givemore;
  }
  
  debug( "blocked\n" );
}

static void
rip_sendto( struct proto *p, ip_addr daddr, int dport, struct rip_interface *rif )
{
  struct iface *iface = rif->iface;
  struct rip_connection *c = mb_alloc( p->pool, sizeof( struct rip_connection ));
  static int num = 0;

  if (rif->busy) {
    log (L_WARN "Interface %s is much too slow, dropping request\n", iface->name);
    return;
  }
  rif->busy = c;
  
  c->addr = daddr;
  c->proto = p;
  c->num = num++;
  c->rif = rif;

  c->dport = dport;
  c->daddr = daddr;
  if (c->rif->sock->data != rif)
    die("not enough send magic\n");
#if 0
  if (sk_open(c->send)<0) {
    log( L_ERR "Could not open socket for data send to %I:%d on %s\n", daddr, dport, rif->iface->name );
    return;
  }
#endif

  c->sendptr = HEAD( P->rtable );  
  add_head( &P->connections, NODE c );
  debug( "Sending my routing table to %I:%d on %s\n", daddr, dport, rif->iface->name );

  rip_tx(c->rif->sock);
}

/*
 * Input processing
 */

static int
process_authentication( struct proto *p, struct rip_block *block )
{
  /* FIXME: Should do md5 authentication */
  return 0;
}

/* Let main routing table know about our new entry */
static void
advertise_entry( struct proto *p, struct rip_block *b, ip_addr whotoldme )
{
  rta *a, A;
  rte *r;
  net *n;
  neighbor *neighbor;

  bzero(&A, sizeof(A));
  A.proto = p;
  A.source = RTS_RIP;
  A.scope = SCOPE_UNIVERSE;
  A.cast = RTC_UNICAST;
  A.dest = RTD_ROUTER;
  A.tos = 0;
  A.flags = 0;
  A.gw = ipa_nonzero(b->nexthop) ? b->nexthop : whotoldme;
  A.from = whotoldme;
  
  neighbor = neigh_find( p, &A.gw, 0 );
  if (!neighbor) {
    log( L_ERR "%I asked me to route %I/%I using not-neighbor %I.\n", A.from, b->network, b->netmask, A.gw );
    return;
  }

  A.iface = neighbor->iface;
  /* set to: interface of nexthop */
  a = rta_lookup(&A);
  n = net_get( &master_table, 0, b->network, ipa_mklen( b->netmask )); /* FIXME: should verify that it really is netmask */
  r = rte_get_temp(a);
  r->u.rip.metric = ntohl(b->metric);
  r->u.rip.tag = ntohl(b->tag);
  r->net = n;
  r->pflags = 0; /* Here go my flags */
  rte_update( n, p, r );
  debug( "done\n" );
}

static void
process_block( struct proto *p, struct rip_block *block, ip_addr whotoldme )
{
  struct rip_entry *e;
  int metric = ntohl( block->metric );
  ip_addr network = block->network;

  CHK_MAGIC;
  if ((!metric) || (metric > P->infinity)) {
    log( L_WARN "Got metric %d from %I\n", metric, whotoldme );
    return;
  }

  /* FIXME: Check if destination looks valid - ie not net 0 or 127 */

  /* FIXME: Should add configurable ammount */
  if (metric < P->infinity)
    metric++;

  debug( "block: %I tells me: %I/%I available, metric %d... ", whotoldme, network, block->netmask, metric );

  advertise_entry( p, block, whotoldme );
}

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
  case RIPCMD_REQUEST: debug( "Asked to send my routing table\n" ); 
    	  rip_sendto( p, whotoldme, port, NULL ); /* no broadcast */
          break;
  case RIPCMD_RESPONSE: debug( "*** Rtable from %I\n", whotoldme ); 
          if (port != RIP_PORT) {
	    log( L_ERR "%I send me routing info from port %d\n", whotoldme, port );
#if 0
	    return 0;
#else
	    log( L_ERR "...ignoring\n" );
#endif
	  }

	  if (!neigh_find( p, &whotoldme, 0 )) {
	    log( L_ERR "%I send me routing info but he is not my neighbour\n", whotoldme );
	    return 0;
	  }

	  /* FIXME: Should check if it is not my own packet */

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
  case RIPCMD_TRACEON:
  case RIPCMD_TRACEOFF: BAD( "I was asked for traceon/traceoff\n" );
  case 5: BAD( "Some Sun extension around here\n" );
  default: BAD( "Unknown command" );
  }

  rip_reply(p);
  return 0;
}

static int
rip_rx(sock *s, int size)
{
  struct rip_interface *i = s->data;
  struct proto *p = i->proto;
  int num;

  CHK_MAGIC;
  debug( "RIP: message came: %d bytes\n", size );
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
  debug( "%I told me %d/%d ago: to %I/%d go via %I, metric %d ", 
  e->whotoldme, e->updated-now, e->changed-now, e->network, e->pxlen, e->nexthop, e->metric );
  if (e->flags & RIP_F_EXTERNAL) debug( "[external]" );
  debug( "\n" );
}

static void
rip_timer(timer *t)
{
  struct proto *p = t->data;
  struct rip_entry *e, *et;

  CHK_MAGIC;
  debug( "RIP: tick tock\n" );
  
  WALK_LIST_DELSAFE( e, et, P->garbage ) {
    rte *rte;
    rte = SKIP_BACK( struct rte, u.rip.garbage, e );
    debug( "Garbage: " ); rte_dump( rte );

    if (now - rte->lastmod > (RIP_GB_TIME)) {
      debug( "RIP: entry is too old: " ); rte_dump( rte );
      rte_discard(rte);
    }
  }

  debug( "RIP: Broadcasting routing tables\n" );
  {
    struct rip_interface *i;

    WALK_LIST( i, P->interfaces ) {
      struct iface *iface = i->iface;

      if (!(iface->flags & IF_UP)) continue;
      if (iface->flags & (IF_IGNORE | IF_LOOPBACK)) continue;

      rip_sendto( p, IPA_NONE, 0, i );
    }
  }

  debug( "RIP: tick tock done\n" );
}

static void
rip_start(struct proto *p)
{
  debug( "RIP: starting instance...\n" );

  P->magic = RIP_MAGIC;
  init_list( &P->rtable );
  init_list( &P->connections );
  init_list( &P->garbage );
  init_list( &P->interfaces );
  P->timer = tm_new( p->pool );
  P->timer->data = p;
  P->timer->randomize = 5;
  P->timer->recurrent = RIP_TIME; 
  P->timer->hook = rip_timer;
  tm_start( P->timer, 5 );
  CHK_MAGIC;

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
  struct rip_interface *rif;
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
  i = 0;
  WALK_LIST( rif, P->interfaces ) {
    debug( "RIP: interface #%d: %s, %I, busy = %x\n", i++, rif->iface->name, rif->sock->daddr, rif->busy );
  }
}

static int
rip_want_this_if(struct rip_interface *iface)
{
  return 1;
}

static void
kill_iface(struct proto *p, struct rip_interface *i)
{
  debug( "RIP: Interface %s disappeared\n", i->iface->name);
  rfree(i->sock);
  mb_free(i);
}

struct rip_interface *
new_iface(struct proto *p, struct iface *new)
{
  struct rip_interface *i;
  debug( "RIP: New interface %s here\n", new->name);
  i = mb_alloc(p->pool, sizeof( struct rip_interface ));
  i->iface = new;
  i->proto = p;

  i->sock = sk_new( p->pool );
  i->sock->type = SK_UDP;
  i->sock->sport = RIP_PORT;
  i->sock->rx_hook = rip_rx;
  i->sock->data = i;
  i->sock->rbsize = 10240;
  i->sock->iface = new;
  i->sock->tbuf = mb_alloc( p->pool, sizeof( struct rip_packet ));
  i->sock->tx_hook = rip_tx;
  i->sock->err_hook = rip_tx_err;
  i->sock->daddr = IPA_NONE;
  i->sock->dport = RIP_PORT;

  if (new->flags & IF_BROADCAST)
    i->sock->daddr = new->brd;
  if (new->flags & IF_UNNUMBERED)
    i->sock->daddr = new->opposite;

  if (!ipa_nonzero(i->sock->daddr))
    log( L_WARN "RIP: interface %s is too strange for me", i->iface->name );

  if (sk_open(i->sock)<0)
    die( "RIP/%s: could not listen on %s\n", p->name, i->iface->name );
  
  return i;
}

static void
rip_if_notify(struct proto *p, unsigned c, struct iface *old, struct iface *new)
{
  debug( "RIP: if notify\n" );
  if (old) {
    struct rip_interface *i;
    WALK_LIST (i, P->interfaces)
      if (i->iface == old) {
	rem_node(NODE i);
	kill_iface(p, i);
      }
  }
  if (new) {
    struct rip_interface *i;
    i = new_iface(p, new);
    add_head( &P->interfaces, NODE i );
  }
}

static void
rip_rt_notify(struct proto *p, struct network *net, struct rte *new, struct rte *old)
{
  CHK_MAGIC;

  if (old) {
    struct rip_entry *e = find_entry( p, net->n.prefix, net->n.pxlen );

    if (!e)
      log( L_ERR "Deleting nonexistent entry?!\n" );

    kill_entry_ourrt( p, e );
  }

  if (new) {
    struct rip_entry *e = new_entry( p );

    e->network = net->n.prefix;
    e->pxlen = net->n.pxlen;
    e->nexthop = new->attrs->gw;
    e->tag = new->u.rip.tag;
    e->metric = new->u.rip.metric;
    e->whotoldme = new->attrs->from;
    e->updated = e->changed = now;
    e->flags = 0;

    add_head( &P->rtable, NODE e );
  }
}

static int
rip_rte_better(struct rte *new, struct rte *old)
{
  if (old->u.rip.metric < new->u.rip.metric)
    return 0;

  if (old->u.rip.metric > new->u.rip.metric)
    return 1;

#define old_metric_is_much_older_than_new_metric 0
  if ((old->u.rip.metric == new->u.rip.metric) && (old_metric_is_much_older_than_new_metric))
    return 1;

  return 0;
}

static int
rip_rta_same(rta *a, rta *b)
{
  /* As we have no specific data in rta, they are always the same */
  return 1;
}

static void
rip_rte_insert(net *net, rte *rte)
{
  struct proto *p = rte->attrs->proto;
  add_head( &P->garbage, &rte->u.rip.garbage );
}

static void
rip_rte_remove(net *net, rte *rte)
{
  struct proto *p = rte->attrs->proto;
  rem_node( &rte->u.rip.garbage );
}

void
rip_init_instance(struct proto *p)
{
  p->preference = DEF_PREF_RIP;
  p->start = rip_start;
  p->if_notify = rip_if_notify;
  p->rt_notify = rip_rt_notify;
  p->rte_better = rip_rte_better;
  p->rta_same = rip_rta_same;
  p->rte_insert = rip_rte_insert;
  p->rte_remove = rip_rte_remove;
  p->dump = rip_dump;
  P->infinity = 16;
}

static void
rip_preconfig(struct protocol *x)
{
  debug( "RIP: preconfig\n" );
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
