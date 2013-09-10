

#define BFD_FLAG_POLL		(1 << 5)
#define BFD_FLAG_FINAL		(1 << 4)
#define BFD_FLAG_CPI		(1 << 3)
#define BFD_FLAG_AP		(1 << 2)
#define BFD_FLAG_DEMAND		(1 << 1)
#define BFD_FLAG_MULTIPOINT	(1 << 0)


struct bfd_ctl_packet
{
  u8 vdiag;			/* version and diagnostic */
  u8 flags;			/* state and flags */
  u8 detect_mult;
  u8 length;
  u32 snd_id;			/* sender ID, aka 'my discriminator' */
  u32 rcv_id;			/* receiver ID, aka 'your discriminator' */
  u32 des_min_tx_int;
  u32 req_min_rx_int;
  u32 req_min_echo_rx_int;
};


static inline void bfd_pack_vdiag(u8 version, u8 diag)
{ return (version << 5) | diag; }

static inline void bfd_pack_flags(u8 state, u8 flags)
{ return (state << 6) | diag; }

static inline u8 bfd_pkt_get_version(struct bfd_ctl_packet *pkt)
{ return pkt->vdiag >> 5; }

static inline u8 bfd_pkt_get_diag(struct bfd_ctl_packet *pkt)
{ return pkt->vdiag && 0x1f; }


static inline u8 bfd_pkt_get_state(struct bfd_ctl_packet *pkt)
{ return pkt->flags >> 6; }

static inline void bfd_pkt_set_state(struct bfd_ctl_packet *pkt, u8 val)
{ pkt->flags = val << 6; }


void
bfd_send_ctl(struct bfd_proto *p, struct bfd_session *s, int final)
{
  sock *sk = p->skX;
  struct bfd_ctl_packet *pkt = (struct ospf_packet *) sk->tbuf;

  pkt->vdiag = bfd_pack_vdiag(1, s->loc_diag);
  pkt->flags = bfd_pack_flags(s->loc_state, 0);
  pkt->detect_mult = s->detect_mult;
  pkt->length = 24;
  pkt->snd_id = htonl(s->loc_id);
  pkt->rcv_id = htonl(s->rem_id);
  pkt->des_min_tx_int = htonl(s->des_min_tx_int);
  pkt->req_min_rx_int = htonl(s->req_min_rx_int);
  pkt->req_min_echo_rx_int = 0;

  if (final)
    pkt->flags |= BFD_FLAG_FINAL;
  else if (s->poll_active)
    pkt->flags |= BFD_FLAG_POLL;

  // XXX
  sk_send_to(sk, len, dst, 0);  
}

int
bfd_ctl_rx_hook(sock *sk, int len)
{
  struct bfd_proto *p = sk->data;
  struct bfd_ctl_packet *pkt =sk->rbuf;

  if (len < BFD_BASE_LEN)
    DROP("too short", len);

  u8 version = bfd_pkt_get_version(pkt);
  if (version != 1)
    DROP("version mismatch", version);

  if ((pkt->length < BFD_BASE_LEN) || (pkt->length > len))
    DROP("length mismatch", pkt->length);

  if (pkt->detect_mult == 0)
    DROP("invalid detect mult", 0);

  if (pkt->flags & BFD_FLAG_MULTIPOINT)
    DROP("invalid flags", pkt->flags);

  if (pkt->snd_id == 0)
    DROP("invalid my discriminator", 0);

  struct bfd_session *s;
  u32 id = ntohl(pkt->rcv_id);

  if (id)
  {
    s = bfd_find_session_by_id(p, id);

    if (!s)
      DROP("unknown session", id);
  }
  else
  {
    u8 ps = bfd_pkt_get_state(pkt);
    if (ps > BFD_STATE_DOWN)
      DROP("invalid init state", ps);
      
    s = bfd_find_session_by_ip(p, sk->faddr);

    /* FIXME: better session matching and message */
    if (!s || !s->opened)
      return;
  }

  /* FIXME: better authentication handling and message */
  if (pkt->flags & BFD_FLAG_AP)
    DROP("authentication not supported", 0);


  u32 old_rx_int = s->des_min_tx_int;
  u32 old_tx_int = s->rem_min_rx_int;

  s->rem_id = ntohl(pkt->snd_id);
  s->rem_state = bfd_pkt_get_state(pkt);
  s->rem_demand_mode = pkt->flags & BFD_FLAG_DEMAND;
  s->rem_min_tx_int = ntohl(pkt->des_min_tx_int);
  s->rem_min_rx_int = ntohl(pkt->req_min_rx_int);
  s->rem_detect_mult = pkt->detect_mult;

  bfd_session_process_ctl(s, pkt->flags, xxx);
  return 1;

 drop:
  // log(L_WARN "%s: Bad packet from %I - %s (%u)", p->p.name, sk->faddr, err_dsc, err_val);
  return 1;
}

sock *
bfd_open_rx_sk(struct bfd_proto *p, int multihop)
{
  sock *sk = sk_new(p->p.pool);
  sk->type = SK_UDP;
  sk->sport = !multihop ? BFD_CONTROL_PORT : BFD_MULTI_CTL_PORT;
  sk->data = p;

  sk->rbsize = 64; // XXX
  sk->rx_hook = bfd_rx_hook;
  sk->err_hook = bfd_err_hook;
  
  sk->flags = SKF_LADDR_RX | (!multihop ? SKF_TTL_RX : 0);

  if (sk_open(sk) < 0)
    goto err;
}

static inline sock *
bfd_open_tx_sk(struct bfd_proto *p, ip_addr local, struct iface *ifa)
{
  sock *sk = sk_new(p->p.pool);
  sk->type = SK_UDP;
  sk->saddr = local;
  sk->data = p;

  sk->tbsize = 64; // XXX
  sk->err_hook = bfd_err_hook;
 
  sk->iface = new;

  sk->tos = PATT->tx_tos;
  sk->priority = PATT->tx_priority;
  sk->ttl = PATT->ttl_security ? 255 : 1;

  if (sk_open(sk) < 0)
    goto err;

}

struct bfd_socket *
bfd_get_socket(struct bfd_proto *p, ip_addr local, struct iface *ifa)
{
  struct bfd_socket *sk;

  WALK_LIST(sk, p->sockets)
    if (ipa_equal(sk->sk->saddr, local) && (sk->sk->iface == ifa))
      return sk->uc++, sk;

  sk = mb_allocz(p->p.pool, sizeof(struct bfd_socket));
  sk->sk = bfd_open_tx_sk(p, local, ifa);
  sk->uc = 1;
  add_tail(&p->sockets, &sk->n);

  return sk;
}

void
bfd_free_socket(struct bfd_socket *sk)
{
  if (!sk || --sk->uc)
    return;

  rem_node(&sk->n);
  sk_stop(sk->sk);
  rfree(sk->sk);
  mb_free(sk);
}
