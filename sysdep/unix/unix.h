/*
 *	BIRD -- Declarations Common to Unix Port
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_UNIX_H_
#define _BIRD_UNIX_H_

struct pool;

/* main.c */

void async_config(void);
void async_dump(void);
void async_shutdown(void);

/* io.c */

volatile int async_config_flag;
volatile int async_dump_flag;
volatile int async_shutdown_flag;

#ifdef IPV6
#define BIRD_PF PF_INET6
#define BIRD_AF AF_INET6
typedef struct sockaddr_in6 sockaddr;
#else
#define BIRD_PF PF_INET
#define BIRD_AF AF_INET
typedef struct sockaddr_in sockaddr;
#endif

struct birdsock;

void io_init(void);
void io_loop(void);
void fill_in_sockaddr(sockaddr *sa, ip_addr a, unsigned port);
void get_sockaddr(sockaddr *sa, ip_addr *a, unsigned *port);
int sk_open_unix(struct birdsock *s, char *name);
void *tracked_fopen(struct pool *, char *name, char *mode);

/* krt.c bits */

void krt_io_init(void);

/* log.c */

void log_init(int debug);
void log_init_debug(char *);		/* Initialize debug dump to given file (NULL=stderr, ""=off) */
void log_switch(struct list *);

struct log_config {
  node n;
  unsigned int mask;			/* Classes to log */
  void *fh;				/* FILE to log to, NULL=syslog */
  int terminal_flag;
};

#endif
