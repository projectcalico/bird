/*
 *	BIRD Internet Routing Daemon -- Unix Entry Point
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <sys/signal.h>

#include "nest/bird.h"
#include "lib/lists.h"
#include "lib/resource.h"
#include "lib/socket.h"
#include "nest/route.h"
#include "nest/protocol.h"
#include "nest/iface.h"

#include "unix.h"

/*
 *	Debugging
 */

static void
handle_sigusr(int sig)
{
  debug("SIGUSR1: Debugging dump...\n\n");

  sk_dump_all();
  tm_dump_all();
  if_dump_all();
  neigh_dump_all();
  rta_dump_all();
  rt_dump_all();

  debug("\n");
}

static void
signal_init(void)
{
  static struct sigaction sa;

  sa.sa_handler = handle_sigusr;
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGUSR1, &sa, NULL) < 0)
    die("sigaction: %m");
  signal(SIGPIPE, SIG_IGN);
}

/*
 *	Hic Est main()
 */

void erro(sock *s, int e)
{
  debug("errrr e=%d\n", e);
  rfree(s);
}

void bla(sock *s)
{
  puts("W");
  strcpy(s->tbuf, "RAM!\r\n");
  sk_send(s, 6);
}

int xxx(sock *s, int h)
{
  puts("R");
  do {
    strcpy(s->tbuf, "Hello, world!\r\n");
  }
  while (sk_send(s, 15) > 0);
  return 1;
}

int
main(void)
{
  log(L_INFO "Launching BIRD -1.-1-pre-omega...");

  log_init_debug(NULL);
  resource_init();
  io_init();
  rt_init();
  if_init();
  protos_init();

  scan_if_init();

  signal_init();

  handle_sigusr(0);

  debug("Entering I/O loop.\n");

  io_loop();
  die("I/O loop died");
}
