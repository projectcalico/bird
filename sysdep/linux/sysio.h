/*
 *	BIRD Internet Routing Daemon -- Linux Multicasting and Network Includes
 *
 *	(c) 1998--2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <net/if.h>

#ifdef IPV6

#ifndef IPV6_UNICAST_HOPS
/* Needed on glibc 2.0 systems */
#include <linux/in6.h>
#define CONFIG_IPV6_GLIBC_20
#endif

static inline void
set_inaddr(struct in6_addr *ia, ip_addr a)
{
  ipa_hton(a);
  memcpy(ia, &a, sizeof(a));
}

static inline void
get_inaddr(ip_addr *a, struct in6_addr *ia)
{
  memcpy(a, ia, sizeof(*a));
  ipa_ntoh(*a);
}

#else

static inline void
set_inaddr(struct in_addr *ia, ip_addr a)
{
  ipa_hton(a);
  memcpy(&ia->s_addr, &a, sizeof(a));
}

static inline void
get_inaddr(ip_addr *a, struct in_addr *ia)
{
  memcpy(a, &ia->s_addr, sizeof(*a));
  ipa_ntoh(*a);
}


#ifndef HAVE_STRUCT_IP_MREQN
/* Several versions of glibc don't define this structure, so we have to do it ourselves */
struct ip_mreqn
{
	struct in_addr	imr_multiaddr;		/* IP multicast address of group */
	struct in_addr	imr_address;		/* local IP address of interface */
	int		imr_ifindex;		/* Interface index */
};
#endif


static inline void fill_mreqn(struct ip_mreqn *m, ip_addr maddr, struct iface *ifa)
{
  bzero(m, sizeof(*m));
  m->imr_ifindex = ifa->index;
  set_inaddr(&m->imr_multiaddr, maddr);
}

static inline char *
sysio_setup_multicast(sock *s)
{
  struct ip_mreqn m;
  int zero = 0;

  if (setsockopt(s->fd, SOL_IP, IP_MULTICAST_LOOP, &zero, sizeof(zero)) < 0)
    return "IP_MULTICAST_LOOP";

  if (setsockopt(s->fd, SOL_IP, IP_MULTICAST_TTL, &s->ttl, sizeof(s->ttl)) < 0)
    return "IP_MULTICAST_TTL";

  /* This defines where should we send _outgoing_ multicasts */
  fill_mreqn(&m, IPA_NONE, s->iface);
  if (setsockopt(s->fd, SOL_IP, IP_MULTICAST_IF, &m, sizeof(m)) < 0)
    return "IP_MULTICAST_IF";

  return NULL;
}

static inline char *
sysio_join_group(sock *s, ip_addr maddr)
{
  struct ip_mreqn m;

  /* And this one sets interface for _receiving_ multicasts from */
  fill_mreqn(&m, maddr, s->iface);
  if (setsockopt(s->fd, SOL_IP, IP_ADD_MEMBERSHIP, &m, sizeof(m)) < 0)
    return "IP_ADD_MEMBERSHIP";

  return NULL;
}

static inline char *
sysio_leave_group(sock *s, ip_addr maddr)
{
  struct ip_mreqn m;

  /* And this one sets interface for _receiving_ multicasts from */
  fill_mreqn(&m, maddr, s->iface);
  if (setsockopt(s->fd, SOL_IP, IP_DROP_MEMBERSHIP, &m, sizeof(m)) < 0)
    return "IP_DROP_MEMBERSHIP";

  return NULL;
}

#endif


/* For the case that we have older libc headers */
/* Copied from Linux kernel file include/linux/tcp.h */

#ifndef TCP_MD5SIG

#define TCP_MD5SIG  14
#define TCP_MD5SIG_MAXKEYLEN 80

#include <linux/types.h>

struct tcp_md5sig {
  struct  sockaddr_storage tcpm_addr;             /* address associated */
  __u16   __tcpm_pad1;                            /* zero */
  __u16   tcpm_keylen;                            /* key length */
  __u32   __tcpm_pad2;                            /* zero */
  __u8    tcpm_key[TCP_MD5SIG_MAXKEYLEN];         /* key (binary) */
};

#endif

static int
sk_set_md5_auth_int(sock *s, sockaddr *sa, char *passwd)
{
  struct tcp_md5sig md5;

  memset(&md5, 0, sizeof(md5));
  memcpy(&md5.tcpm_addr, (struct sockaddr *) sa, sizeof(*sa));

  if (passwd)
    {
      int len = strlen(passwd);

      if (len > TCP_MD5SIG_MAXKEYLEN)
	{
	  log(L_ERR "MD5 password too long");
	  return -1;
	}

      md5.tcpm_keylen = len;
      memcpy(&md5.tcpm_key, passwd, len);
    }

  int rv = setsockopt(s->fd, SOL_TCP, TCP_MD5SIG, &md5, sizeof(md5));

  if (rv < 0) 
    {
      if (errno == ENOPROTOOPT)
	log(L_ERR "Kernel does not support TCP MD5 signatures");
      else
	log(L_ERR "sk_set_md5_auth_int: setsockopt: %m");
    }

  return rv;
}


#ifndef IPV6

/* RX/TX packet info handling for IPv4 */
/* Mostly similar to standardized IPv6 code */

#define CMSG_RX_SPACE (CMSG_SPACE(sizeof(struct in_pktinfo)) + CMSG_SPACE(sizeof(int)))
#define CMSG_TX_SPACE CMSG_SPACE(sizeof(struct in_pktinfo))

static char *
sysio_register_cmsgs(sock *s)
{
  int ok = 1;

  if ((s->flags & SKF_LADDR_RX) &&
      (setsockopt(s->fd, SOL_IP, IP_PKTINFO, &ok, sizeof(ok)) < 0))
    return "IP_PKTINFO";

  if ((s->flags & SKF_TTL_RX) &&
      (setsockopt(s->fd, SOL_IP, IP_RECVTTL, &ok, sizeof(ok)) < 0))
    return "IP_RECVTTL";

  return NULL;
}

static void
sysio_process_rx_cmsgs(sock *s, struct msghdr *msg)
{
  struct cmsghdr *cm;
  struct in_pktinfo *pi = NULL;
  int *ttl = NULL;

  for (cm = CMSG_FIRSTHDR(msg); cm != NULL; cm = CMSG_NXTHDR(msg, cm))
  {
    if (cm->cmsg_level == SOL_IP && cm->cmsg_type == IP_PKTINFO)
      pi = (struct in_pktinfo *) CMSG_DATA(cm);

    if (cm->cmsg_level == SOL_IP && cm->cmsg_type == IP_TTL)
      ttl = (int *) CMSG_DATA(cm);
  }

  if (s->flags & SKF_LADDR_RX)
  {
    if (pi)
    {
      get_inaddr(&s->laddr, &pi->ipi_addr);
      s->lifindex = pi->ipi_ifindex;
    }
    else
    {
      s->laddr = IPA_NONE;
      s->lifindex = 0;
    }
  }

  if (s->flags & SKF_TTL_RX)
    s->ttl = ttl ? *ttl : -1;

  return;
}

static void
sysio_prepare_tx_cmsgs(sock *s, struct msghdr *msg, void *cbuf, size_t cbuflen)
{
  struct cmsghdr *cm;
  struct in_pktinfo *pi;

  msg->msg_control = cbuf;
  msg->msg_controllen = cbuflen;

  cm = CMSG_FIRSTHDR(msg);
  cm->cmsg_level = SOL_IP;
  cm->cmsg_type = IP_PKTINFO;
  cm->cmsg_len = CMSG_LEN(sizeof(*pi));

  pi = (struct in_pktinfo *) CMSG_DATA(cm);
  pi->ipi_ifindex = s->iface ? s->iface->index : 0;
  set_inaddr(&pi->ipi_spec_dst, s->saddr);
  set_inaddr(&pi->ipi_addr, IPA_NONE);

  msg->msg_controllen = cm->cmsg_len;
}


#endif


#ifndef IP_MINTTL
#define IP_MINTTL 21
#endif

#ifndef IPV6_MINHOPCOUNT
#define IPV6_MINHOPCOUNT 73
#endif


#ifndef IPV6

static int
sk_set_min_ttl4(sock *s, int ttl)
{
  if (setsockopt(s->fd, SOL_IP, IP_MINTTL, &ttl, sizeof(ttl)) < 0)
  {
    if (errno == ENOPROTOOPT)
      log(L_ERR "Kernel does not support IPv4 TTL security");
    else
      log(L_ERR "sk_set_min_ttl4: setsockopt: %m");

    return -1;
  }

  return 0;
}

#else

static int
sk_set_min_ttl6(sock *s, int ttl)
{
  if (setsockopt(s->fd, SOL_IPV6, IPV6_MINHOPCOUNT, &ttl, sizeof(ttl)) < 0)
  {
    if (errno == ENOPROTOOPT)
      log(L_ERR "Kernel does not support IPv6 TTL security");
    else
      log(L_ERR "sk_set_min_ttl6: setsockopt: %m");

    return -1;
  }

  return 0;
}

#endif


#ifndef IPV6_TCLASS
#define IPV6_TCLASS 67
#endif

int sk_priority_control = 7;

static int
sk_set_priority(sock *s, int prio)
{
  if (setsockopt(s->fd, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(prio)) < 0)
  {
    log(L_WARN "sk_set_priority: setsockopt: %m");
    return -1;
  }

  return 0;
}
