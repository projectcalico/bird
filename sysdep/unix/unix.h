/*
 *	BIRD -- Declarations Common to Unix Port
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_UNIX_H_
#define _BIRD_UNIX_H_

/* main.c */

void async_config(void);
void async_dump(void);
void async_shutdown(void);

/* io.c */

volatile int async_config_flag;
volatile int async_dump_flag;
volatile int async_shutdown_flag;

void io_init(void);
void io_loop(void);
void fill_in_sockaddr(struct sockaddr_in *sa, ip_addr a, unsigned port);
void get_sockaddr(struct sockaddr_in *sa, ip_addr *a, unsigned *port);

#endif
