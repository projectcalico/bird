/*
 *	BIRD Internet Routing Daemon -- Unix Entry Point
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/signal.h>

#include "nest/bird.h"
#include "lib/lists.h"
#include "lib/resource.h"
#include "lib/socket.h"
#include "nest/route.h"
#include "nest/protocol.h"
#include "nest/iface.h"
#include "conf/conf.h"
#include "filter/filter.h"

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
 *	Reading the Configuration
 */

static int conf_fd;

static int
cf_read(byte *dest, unsigned int len)
{
  int l = read(conf_fd, dest, len);
  if (l < 0)
    cf_error("Read error");
  return l;
}

static void
read_config(void)
{
  cf_lex_init_tables();
  cf_allocate();
  conf_fd = open(PATH_CONFIG, O_RDONLY);
  if (conf_fd < 0)
    die("Unable to open configuration file " PATH_CONFIG ": %m");
  protos_preconfig();
  cf_read_hook = cf_read;
  cf_lex_init(1);
  cf_parse();
  filters_postconfig();
  protos_postconfig();
}
/*
 *	Hic Est main()
 */

int
main(void)
{
  log(L_INFO "Launching BIRD 0.0.0...");

  log_init_debug(NULL);

  debug("Initializing.\n");
  resource_init();
  io_init();
  rt_init();
  if_init();

  protos_build();
  add_tail(&protocol_list, &proto_unix_kernel.n);
  protos_init();

  debug("Reading configuration file.\n");
  read_config();

  signal_init();

  scan_if_init();
  auto_router_id();

  protos_start();

  handle_sigusr(0);

  debug("Entering I/O loop.\n");

  io_loop();
  bug("I/O loop died");
}
