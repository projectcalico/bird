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
#include "nest/confile.h"

#include "unix.h"
#include "krt.h"

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
  protos_dump_all();

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
 *	Config Pool
 */

pool *cfg_pool;
mempool *cfg_mem;

/*
 *	Hic Est main()
 */

int
main(void)
{
  log(L_INFO "Launching BIRD -1.-1-pre-omega...");

  log_init_debug(NULL);
  resource_init();
  cfg_pool = rp_new(&root_pool, "Config");
  cfg_mem = mp_new(cfg_pool, 1024);

  io_init();
  rt_init();
  if_init();
  protos_build();
  add_tail(&protocol_list, &proto_unix_kernel.n); /* FIXME: Must be _always_ the last one */
  protos_init();
  protos_preconfig();
  protos_postconfig();

  signal_init();

  scan_if_init();
  auto_router_id();

  protos_start();

  handle_sigusr(0);

  debug("Entering I/O loop.\n");

  io_loop();
  die("I/O loop died");
}
