/*
 *	Rest in pieces - RIP protocol
 *
 *	Copyright (c) 1998, 1999 Pavel Machek <pavel@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 *
 	FIXME: IpV6 support: packet size
	FIXME: (nonurgent) IpV6 support: receive "route using" blocks
	FIXME: (nonurgent) IpV6 support: generate "nexthop" blocks
		next hops are only advisory, and they are pretty ugly in IpV6.
		I suggest just forgetting about them.

	FIXME (nonurgent): fold rip_connection into rip_interface?

	FIXME: (nonurgent) allow bigger frequencies than 1 regular update in 6 seconds (?)
	FIXME: propagation of metric=infinity into main routing table may or may not be good idea.

 */

/**
 * DOC: Routing information protocol
 *
 * Rip is pretty simple protocol so half of this code is interface
 * with core. We maintain our own linklist of &rip_entry - it serves
 * as our small routing table. Within rip_tx(), this list is
 * walked, and packet is generated using rip_tx_prepare(). This gets
 * tricky because we may need to send more than one packet to one
 * destination. Struct &rip_connection is used to hold info such as how
 * many of &rip_entry ies we already send, and is also used to protect
 * from two concurrent sends to one destination. Each &rip_interface has
 * at most one &rip_connection.
 *
 * We are not going to honor requests for sending part of
 * routing table. That would need to turn split horizon off,
 * etc.  
 *
 * Triggered updates. RFC says: when triggered update was sent, don't send
 * new one for something between 1 and 5 seconds (and send one
 * after that). We do something else: once in 5 second
 * we look for any changed routes and broadcast them.
 */


#define LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/protocol.h"
#include "nest/route.h"
#include "lib/socket.h"
#include "lib/resource.h"
#include "lib/lists.h"
#include "lib/timer.h"
#include "lib/string.h"

#include "rip.h"

#define P ((struct rip_proto *) p)
#define P_CF ((struct rip_proto_config *)p->cf)
#define E ((struct rip_entry *) e)

#define TRACE(level, msg, args...) do { if (p->debug & level) { log(L_TRACE "%s: " msg, p->name , ## args); } } while(0)

static struct rip_interface *new_iface(struct proto *p, struct iface *new, unsigned long flags, struct iface_patt *patt);

#define P_NAME p->name

/*
 * Output processing
 */

static void
rip_tx_err( sock *s, int err )
{
  struct rip_connection *c = s->data;
  struct proto *p = c->proto;
  log( L_ERR "Unexpected error at rip transmit: %M", err );
}

static int
rip_tx_prepare(struct proto *p, ip_addr daddr, struct rip_block *b, struct rip_entry *e, struct rip_interface *rif, int pos )
{
  DBG( "." );
  b->tag     = htons( e->tag );
  b->network = e->n.prefix;
#ifndef IPV6
  b->family  = htons( 2 ); /* AF_INET */
  b->netmask = ipa_mkmask( e->n.pxlen );
  ipa_hton( b->netmask );

  if (neigh_connected_to(p, &e->nexthop, rif->iface))
    b->nexthop = e->nexthop;
  else
    b->nexthop = IPA_NONE;
  ipa_hton( b->nexthop );
#else
  b->pxlen = e->n.pxlen;
#endif
  b->metric  = htonl( e->metric );
  if (neigh_connected_to(p, &e->whotoldme, rif->iface)) {
    DBG( "(split horizon)" );
    b->metric = htonl( P_CF->infinity );
  }
  ipa_hton( b->network );

  return pos+1;
}

static void
rip_tx( sock *s )
{
  struct rip_interface *rif = s->data;
  struct rip_connection *c = rif->busy;
  struct proto *p = c->proto;
  struct rip_packet *packet = (void *) s->tbuf;
  int i, packetlen;
  int maxi, nullupdate = 1;

  DBG( "Sending to %I\n", s->daddr );
  do {

    if (c->done)
      goto done;

    DBG( "Preparing packet to send: " );

    packet->heading.command = RIPCMD_RESPONSE;
    packet->heading.version = RIP_V2;
    packet->heading.unused  = 0;

    i = !!P_CF->authtype;
#ifndef IPV6
    maxi = ((P_CF->authtype == AT_MD5) ? PACKET_MD5_MAX : PACKET_MAX);
#else
    maxi = 5; /* We need to have at least reserve of one at end of packet */
#endif
    
    FIB_ITERATE_START(&P->rtable, &c->iter, z) {
      struct rip_entry *e = (struct rip_entry *) z;

      if (!rif->triggered || (!(e->updated < now-5))) {
	nullupdate = 0;
	i = rip_tx_prepare( p, s->daddr, packet->block + i, e, rif, i );
	if (i >= maxi) {
	  FIB_ITERATE_PUT(&c->iter, z);
	  goto break_loop;
	}
      }
    } FIB_ITERATE_END(z);
    c->done = 1;

  break_loop:

    packetlen = rip_outgoing_authentication(p, (void *) &packet->block[0], packet, i);

    DBG( ", sending %d blocks, ", i );
    if (nullupdate) {
      DBG( "not sending NULL update\n" );
      c->done = 1;
      goto done;
    }
    if (ipa_nonzero(c->daddr))
      i = sk_send_to( s, packetlen, c->daddr, c->dport );
    else
      i = sk_send( s, packetlen );

    DBG( "it wants more\n" );
  
  } while (i>0);
  
  if (i<0) rip_tx_err( s, i );
  DBG( "blocked\n" );
  return;

done:
  DBG( "Looks like I'm" );
  c->rif->busy = NULL;
  rem_node(NODE c);
  mb_free(c);
  DBG( " done\n" );
  return;
}

static void
rip_sendto( struct proto *p, ip_addr daddr, int dport, struct rip_interface *rif )
{
  struct iface *iface = rif->iface;
  struct rip_connection *c;
  static int num = 0;

  if (rif->busy) {
    log (L_WARN "Interface %s is much too slow, dropping request", iface->name);
    return;
  }
  c = mb_alloc( p->pool, sizeof( struct rip_connection ));
  rif->busy = c;
  
  c->addr = daddr;
  c->proto = p;
  c->num = num++;
  c->rif = rif;

  c->dport = dport;
  c->daddr = daddr;
  if (c->rif->sock->data != rif)
    bug("not enough send magic");
#if 0
  if (sk_open(c->send)<0) {
    log( L_ERR "Could not open socket for data send to %I:%d on %s", daddr, dport, rif->iface->name );
    return;
  }
#endif

  c->done = 0;
  fit_init( &c->iter, &P->rtable );
  add_head( &P->connections, NODE c );
  TRACE(D_PACKETS, "Sending my routing table to %I:%d on %s\n", daddr, dport, rif->iface->name );

  rip_tx(c->rif->sock);
}

static struct rip_interface*
find_interface(struct proto *p, struct iface *what)
{
  struct rip_interface *i;

  WALK_LIST (i, P->interfaces)
    if (i->iface == what)
      return i;
  return NULL;
}

/*
 * Input processing
 */

/* Let main routing table know about our new entry */
static void
advertise_entry( struct proto *p, struct rip_block *b, ip_addr whotoldme )
{
  rta *a, A;
  rte *r;
  net *n;
  neighbor *neighbor;
  struct rip_interface *rif;
  int pxlen;

  bzero(&A, sizeof(A));
  A.proto = p;
  A.source = RTS_RIP;
  A.scope = SCOPE_UNIVERSE;
  A.cast = RTC_UNICAST;
  A.dest = RTD_ROUTER;
  A.flags = 0;
#ifndef IPV6
  A.gw = ipa_nonzero(b->nexthop) ? b->nexthop : whotoldme;
  pxlen = ipa_mklen(b->netmask);
#else
  A.gw = whotoldme; /* FIXME: next hop is in other packet for v6 */
  pxlen = b->pxlen;
#endif
  A.from = whotoldme;

  /* No need to look if destination looks valid - ie not net 0 or 127 -- core will do for us. */

  neighbor = neigh_find( p, &A.gw, 0 );
  if (!neighbor) {
    log( L_REMOTE "%I asked me to route %I/%d using not-neighbor %I.", A.from, b->network, pxlen, A.gw );
    return;
  }

  A.iface = neighbor->iface;
  if (!(rif = neighbor->data)) {
    rif = neighbor->data = find_interface(p, A.iface);
  }
  if (!rif) {
    bug("Route packet using unknown interface? No.");
    return;
  }
    
  /* set to: interface of nexthop */
  a = rta_lookup(&A);
  if (pxlen==-1)  {
    log( L_REMOTE "%I gave me invalid pxlen/netmask for %I.", A.from, b->network );
    return;
  }
  n = net_get( p->table, b->network, pxlen );
  r = rte_get_temp(a);
  r->u.rip.metric = ntohl(b->metric) + rif->patt->metric;
  if (r->u.rip.metric > P_CF->infinity) r->u.rip.metric = P_CF->infinity;
  r->u.rip.tag = ntohl(b->tag);
  r->net = n;
  r->pflags = 0; /* Here go my flags */
  rte_update( p->table, n, p, r );
  DBG( "done\n" );
}

static void
process_block( struct proto *p, struct rip_block *block, ip_addr whotoldme )
{
  int metric = ntohl( block->metric );
  ip_addr network = block->network;

  CHK_MAGIC;
  TRACE(D_ROUTES, "block: %I tells me: %I/??? available, metric %d... ", whotoldme, network, metric );
  if ((!metric) || (metric > P_CF->infinity)) {
#ifdef IPV6 /* Someone is sedning us nexthop and we are ignoring it */
    if (metric == 0xff)
      { debug( "IpV6 nexthop ignored" ); return; }
#endif
    log( L_WARN "Got metric %d from %I", metric, whotoldme );
    return;
  }

  advertise_entry( p, block, whotoldme );
}

#define BAD( x ) { log( L_REMOTE "%s: " x, P_NAME ); return 1; }

static int
rip_process_packet( struct proto *p, struct rip_packet *packet, int num, ip_addr whotoldme, int port )
{
  int i;
  int native_class = 0, authenticated = 0;

  switch( packet->heading.version ) {
  case RIP_V1: DBG( "Rip1: " ); break;
  case RIP_V2: DBG( "Rip2: " ); break;
  default: BAD( "Unknown version" );
  }

  switch( packet->heading.command ) {
  case RIPCMD_REQUEST: DBG( "Asked to send my routing table\n" ); 
	  if (P_CF->honor == HO_NEVER) {
	    log( L_REMOTE "They asked me to send routing table, but I was told not to do it" );
	    return 0;
	  }
	  if ((P_CF->honor == HO_NEIGHBOR) && (!neigh_find( p, &whotoldme, 0 ))) {
	    log( L_REMOTE "They asked me to send routing table, but he is not my neighbor" );
	    return 0;
	  }
    	  rip_sendto( p, whotoldme, port, HEAD(P->interfaces) ); /* no broadcast */
          break;
  case RIPCMD_RESPONSE: DBG( "*** Rtable from %I\n", whotoldme ); 
          if (port != P_CF->port) {
	    log( L_REMOTE "%I send me routing info from port %d", whotoldme, port );
#if 0
	    return 0;
#else
	    log( L_REMOTE "...ignoring" );
#endif
	  }

	  if (!neigh_find( p, &whotoldme, 0 )) {
	    log( L_REMOTE "%I send me routing info but he is not my neighbor", whotoldme );
#if 0
	    return 0;
#else
	    log( L_REMOTE "...ignoring" );
#endif
	  }

          for (i=0; i<num; i++) {
	    struct rip_block *block = &packet->block[i];
#ifndef IPV6
	    /* Authentication is not defined for v6 */
	    if (block->family == 0xffff) {
	      if (i)
		continue;	/* md5 tail has this family */
	      if (rip_incoming_authentication(p, (void *) block, packet, num, whotoldme))
		BAD( "Authentication failed" );
	      authenticated = 1;
	      continue;
	    }
#endif
	    if ((!authenticated) && (P_CF->authtype != AT_NONE))
	      BAD( "Packet is not authenticated and it should be" );
	    ipa_ntoh( block->network );
#ifndef IPV6
	    ipa_ntoh( block->netmask );
	    ipa_ntoh( block->nexthop );
	    if (packet->heading.version == RIP_V1)	/* FIXME (nonurgent): switch to disable this? */
	      block->netmask = ipa_class_mask(block->network);
#endif
	    process_block( p, block, whotoldme );
	  }
          break;
  case RIPCMD_TRACEON:
  case RIPCMD_TRACEOFF: BAD( "I was asked for traceon/traceoff" );
  case 5: BAD( "Some Sun extension around here" );
  default: BAD( "Unknown command" );
  }

  return 0;
}

static int
rip_rx(sock *s, int size)
{
  struct rip_interface *i = s->data;
  struct proto *p = i->proto;
  int num;

  CHK_MAGIC;
  DBG( "RIP: message came: %d bytes\n", size );
  size -= sizeof( struct rip_packet_heading );
  if (size < 0) BAD( "Too small packet" );
  if (size % sizeof( struct rip_block )) BAD( "Odd sized packet" );
  num = size / sizeof( struct rip_block );
  if (num>PACKET_MAX) BAD( "Too many blocks" );

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
  e->whotoldme, e->updated-now, e->changed-now, e->n.prefix, e->n.pxlen, e->nexthop, e->metric );
  if (e->flags & RIP_F_EXTERNAL) debug( "[external]" );
  debug( "\n" );
}

static void
rip_timer(timer *t)
{
  struct proto *p = t->data;
  struct rip_entry *e, *et;

  CHK_MAGIC;
  DBG( "RIP: tick tock\n" );
  
  WALK_LIST_DELSAFE( e, et, P->garbage ) {
    rte *rte;
    rte = SKIP_BACK( struct rte, u.rip.garbage, e );
    DBG( "Garbage: " ); rte_dump( rte );

    if (now - rte->u.rip.lastmodX > P_CF->timeout_time) {
      TRACE(D_EVENTS, "RIP: entry is too old: %I", rte->net->n.prefix );
      e->metric = P_CF->infinity;
    }

    if (now - rte->u.rip.lastmodX > P_CF->garbage_time) {
      TRACE(D_EVENTS, "RIP: entry is much too old: %I", rte->net->n.prefix );
      rte_discard(p->table, rte);
    }
  }

  DBG( "RIP: Broadcasting routing tables\n" );
  {
    struct rip_interface *rif;
    P->tx_count ++;

    WALK_LIST( rif, P->interfaces ) {
      struct iface *iface = rif->iface;

      if (!iface) continue;
      if (rif->patt->mode & IM_QUIET) continue;
      if (!(iface->flags & IF_UP)) continue;

      rif->triggered = (P->tx_count % 6);
      rip_sendto( p, IPA_NONE, 0, rif );
    }
  }

  DBG( "RIP: tick tock done\n" );
}

/**
 * rip_start - initialize instance of rip
 */
static int
rip_start(struct proto *p)
{
  struct rip_interface *rif;
  DBG( "RIP: starting instance...\n" );

  P->magic = RIP_MAGIC;
  fib_init( &P->rtable, p->pool, sizeof( struct rip_entry ), 0, NULL );
  init_list( &P->connections );
  init_list( &P->garbage );
  init_list( &P->interfaces );
  P->timer = tm_new( p->pool );
  P->timer->data = p;
  P->timer->randomize = 5;
  P->timer->recurrent = (P_CF->period / 6)+1; 
  P->timer->hook = rip_timer;
  tm_start( P->timer, 5 );
  rif = new_iface(p, NULL, 0, NULL);	/* Initialize dummy interface */
  add_head( &P->interfaces, NODE rif );
  CHK_MAGIC;

  rip_init_instance(p);

  DBG( "RIP: ...done\n");
  return PS_UP;
}

static struct proto *
rip_init(struct proto_config *cfg)
{
  struct proto *p = proto_new(cfg, sizeof(struct rip_proto));

  return p;
}

static void
rip_dump(struct proto *p)
{
  int i;
  node *w, *e;
  struct rip_interface *rif;
  i = 0;

  CHK_MAGIC;
  WALK_LIST( w, P->connections ) {
    struct rip_connection *n = (void *) w;
    debug( "RIP: connection #%d: %I\n", n->num, n->addr );
  }
  i = 0;
  FIB_WALK( &P->rtable, e ) {
    debug( "RIP: entry #%d: ", i++ );
    rip_dump_entry( E );
  } FIB_WALK_END;
  i = 0;
  WALK_LIST( rif, P->interfaces ) {
    debug( "RIP: interface #%d: %s, %I, busy = %x\n", i++, rif->iface?rif->iface->name:"(dummy)", rif->sock->daddr, rif->busy );
  }
}

static void
rip_get_route_info(rte *rte, byte *buf)
{
  buf += bsprintf(buf, " (%d/%d)", rte->pref, rte->u.rip.metric );
  bsprintf(buf, " t%04x", rte->u.rip.tag );
}

static int
rip_want_this_if(struct rip_interface *iface)
{
  return 1;
}

static void
kill_iface(struct proto *p, struct rip_interface *i)
{
  DBG( "RIP: Interface %s disappeared\n", i->iface->name);
  rfree(i->sock);
  mb_free(i);
}

/**
 * new_iface - actually create struct interface and start listening to it
 * @new: interface to be created or %NULL if we are creating magic
 * socket. Magic socket is used for listening, and is also used for
 * sending requested responses. 
 */
static struct rip_interface *
new_iface(struct proto *p, struct iface *new, unsigned long flags, struct iface_patt *patt )
{
  struct rip_interface *rif;

  rif = mb_allocz(p->pool, sizeof( struct rip_interface ));
  rif->iface = new;
  rif->proto = p;
  rif->busy = NULL;
  rif->patt = (struct rip_patt *) patt;

  if (rif->patt)
    rif->multicast = (!(rif->patt->mode & IM_BROADCAST)) && (flags & IF_MULTICAST);
  /* lookup multicasts over unnumbered links - no: rip is not defined over unnumbered links */

  if (rif->multicast)
    DBG( "Doing multicasts!\n" );

  rif->sock = sk_new( p->pool );
  rif->sock->type = rif->multicast?SK_UDP_MC:SK_UDP;
  rif->sock->sport = P_CF->port;
  rif->sock->rx_hook = rip_rx;
  rif->sock->data = rif;
  rif->sock->rbsize = 10240;
  rif->sock->iface = new;		/* Automagically works for dummy interface */
  rif->sock->tbuf = mb_alloc( p->pool, sizeof( struct rip_packet ));
  rif->sock->tx_hook = rip_tx;
  rif->sock->err_hook = rip_tx_err;
  rif->sock->daddr = IPA_NONE;
  rif->sock->dport = P_CF->port;
  if (new)
    rif->sock->ttl = 1;
  else
    rif->sock->ttl = 30;
  rif->sock->tos = IP_PREC_INTERNET_CONTROL;

  if (new) {
    if (new->addr->flags & IA_UNNUMBERED)
      log( L_WARN "%s: rip is not defined over unnumbered links", P_NAME );
    if (rif->multicast) {
#ifndef IPV6
      rif->sock->daddr = ipa_from_u32(0xe0000009);
      rif->sock->saddr = ipa_from_u32(0xe0000009);
#else
      ip_pton("FF02::9", &rif->sock->daddr);
      ip_pton("FF02::9", &rif->sock->saddr);
#endif
    } else
      rif->sock->daddr = new->addr->brd;
  }

  if (!ipa_nonzero(rif->sock->daddr)) {
    log( L_WARN "%s: interface %s is too strange for me", P_NAME, rif->iface ? rif->iface->name : "(dummy)" );
  } else
    if (!(rif->patt->mode & IM_NOLISTEN))
      if (sk_open(rif->sock)<0) {
	log( L_ERR "%s: could not listen on %s", P_NAME, rif->iface ? rif->iface->name : "(dummy)" );
	/* Don't try to transmit into this one? Well, why not? This should not happen, anyway :-) */
      }

  TRACE(D_EVENTS, "%s: listening on %s, port %d, mode %s (%I)", P_NAME, rif->iface ? rif->iface->name : "(dummy)", P_CF->port, rif->multicast ? "multicast" : "broadcast", rif->sock->daddr );
  
  return rif;
}

static void
rip_real_if_add(struct object_lock *lock)
{
  struct iface *iface = lock->iface;
  struct proto *p = lock->data;
  struct rip_interface *rif;
  struct iface_patt *k = iface_patt_match(&P_CF->iface_list, iface);

  if (!k)
    bug("This can not happen! It existed few seconds ago!" );
  DBG("adding interface %s\n", iface->name );
  rif = new_iface(p, iface, iface->flags, k);
  add_head( &P->interfaces, NODE rif );
  rif->lock = lock;
}

static void
rip_if_notify(struct proto *p, unsigned c, struct iface *iface)
{
  DBG( "RIP: if notify\n" );
  if (iface->flags & IF_IGNORE)
    return;
  if (c & IF_CHANGE_DOWN) {
    struct rip_interface *i;
    i = find_interface(p, iface);
    if (i) {
      rem_node(NODE i);
      kill_iface(p, i);
      rfree(i->lock);
    }
  }
  if (c & IF_CHANGE_UP) {
    struct rip_interface *rif;
    struct iface_patt *k = iface_patt_match(&P_CF->iface_list, iface);
    struct object_lock *lock;

    if (!k) return; /* We are not interested in this interface */
    
    lock = olock_new( p->pool );
#ifndef IPV6
    lock->addr = ipa_from_u32(0xe0000009);	/* This is okay: we
						   may actually use
						   other address, but
						   we do not want two
						   rips at one time,
						   anyway. */
#else
    ip_pton("FF02::9", &lock->addr);
#endif
    lock->port = P_CF->port;
    lock->iface = iface;
    lock->hook = rip_real_if_add;
    lock->data = p;
    lock->type = OBJLOCK_UDP;
    olock_acquire(lock);
  }
}

static struct ea_list *
rip_gen_attrs(struct proto *p, struct linpool *pool, int metric, u16 tag)
{
  struct ea_list *l = lp_alloc(pool, sizeof(struct ea_list) + 2*sizeof(eattr));

  l->next = NULL;
  l->flags = EALF_SORTED;
  l->count = 2;
  l->attrs[0].id = EA_RIP_TAG;
  l->attrs[0].flags = 0;
  l->attrs[0].type = EAF_TYPE_INT | EAF_TEMP;
  l->attrs[0].u.data = tag;
  l->attrs[1].id = EA_RIP_METRIC;
  l->attrs[1].flags = 0;
  l->attrs[1].type = EAF_TYPE_INT | EAF_TEMP;
  l->attrs[1].u.data = metric;
  return l;
}

static int
rip_import_control(struct proto *p, struct rte **rt, struct ea_list **attrs, struct linpool *pool)
{
  if ((*rt)->attrs->proto == p)	/* My own must not be touched */
    return 1;

  if ((*rt)->attrs->source != RTS_RIP) {
    struct ea_list *new = rip_gen_attrs(p, pool, 1, 0);
    new->next = *attrs;
    *attrs = new;
  }
  return 0;
}

static struct ea_list *
rip_make_tmp_attrs(struct rte *rt, struct linpool *pool)
{
  struct proto *p = rt->attrs->proto;
  return rip_gen_attrs(p, pool, rt->u.rip.metric, rt->u.rip.tag);
}

static void 
rip_store_tmp_attrs(struct rte *rt, struct ea_list *attrs)
{
  struct proto *p = rt->attrs->proto;

  rt->u.rip.tag = ea_find(attrs, EA_RIP_TAG)->u.data;
  rt->u.rip.metric = ea_find(attrs, EA_RIP_METRIC)->u.data;
}

static void
rip_rt_notify(struct proto *p, struct network *net, struct rte *new, struct rte *old, struct ea_list *attrs)
{
  CHK_MAGIC;

  if (old) {
    struct rip_entry *e = fib_find( &P->rtable, &net->n.prefix, net->n.pxlen );
    if (!e)
      log( L_BUG "Deleting nonexistent entry?!" );
    fib_delete( &P->rtable, e );
  }

  if (new) {
    struct rip_entry *e;
    if (fib_find( &P->rtable, &net->n.prefix, net->n.pxlen ))
      log( L_BUG "Inserting entry which is already there?" );
    e = fib_get( &P->rtable, &net->n.prefix, net->n.pxlen );

    e->nexthop = new->attrs->gw;
    e->metric = 0;
    e->whotoldme = IPA_NONE;

    e->tag = ea_find(attrs, EA_RIP_TAG)->u.data;
    e->metric = ea_find(attrs, EA_RIP_METRIC)->u.data;
    if (e->metric > P_CF->infinity)
      e->metric = P_CF->infinity;

    if (new->attrs->proto == p)
      e->whotoldme = new->attrs->from;

    if (!e->metric)	/* That's okay: this way user can set his own value for external
			   routes in rip. */
      e->metric = 5;
    e->updated = e->changed = now;
    e->flags = 0;
  }
}

static int
rip_rte_better(struct rte *new, struct rte *old)
{
  struct proto *p = new->attrs->proto;

  if (ipa_equal(old->attrs->from, new->attrs->from))
    return 1;

  if (old->u.rip.metric < new->u.rip.metric)
    return 0;

  if (old->u.rip.metric > new->u.rip.metric)
    return 1;

  if ((old->u.rip.metric < 16) && (new->u.rip.metric == P_CF->infinity)) {
    new->u.rip.lastmodX = now - P_CF->timeout_time;	/* Check this: if new metric is 16, act as it was timed out */
  }

  if (old->attrs->proto == new->attrs->proto)		/* This does not make much sense for different protocols */
    if ((old->u.rip.metric == new->u.rip.metric) &&
	((now - old->u.rip.lastmodX) > (P_CF->timeout_time / 2)))
      return 1;

  return 0;
}

static void
rip_rte_insert(net *net, rte *rte)
{
  struct proto *p = rte->attrs->proto;
  rte->u.rip.lastmodX = now;
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
  p->if_notify = rip_if_notify;
  p->rt_notify = rip_rt_notify;
  p->import_control = rip_import_control;
  p->make_tmp_attrs = rip_make_tmp_attrs;
  p->store_tmp_attrs = rip_store_tmp_attrs;
  p->rte_better = rip_rte_better;
  p->rte_insert = rip_rte_insert;
  p->rte_remove = rip_rte_remove;
}

void
rip_init_config(struct rip_proto_config *c)
{
  init_list(&c->iface_list);
  c->infinity	= 16;
  c->port	= 520;
  c->period	= 30;
  c->garbage_time = 120+180;
  c->timeout_time = 120;
  c->passwords	= NULL;
  c->authtype	= AT_NONE;
}

static void
rip_preconfig(struct protocol *x, struct config *c)
{
  DBG( "RIP: preconfig\n" );
}

static int
rip_get_attr(eattr *a, byte *buf)
{
  unsigned int i = EA_ID(a->id);
  struct attr_desc *d;

  switch (a->id) {
  case EA_RIP_METRIC: buf += bsprintf( buf, "metric: %d", a->u.data ); return GA_FULL;
  case EA_RIP_TAG:    buf += bsprintf( buf, "tag: %d", a->u.data );    return GA_FULL;
  default: return GA_UNKNOWN;
  }
}

struct protocol proto_rip = {
  name: "RIP",
  template: "rip%d",
  attr_class: EAP_RIP,
  preconfig: rip_preconfig,
  get_route_info: rip_get_route_info,
  get_attr: rip_get_attr,

  init: rip_init,
  dump: rip_dump,
  start: rip_start,
};
