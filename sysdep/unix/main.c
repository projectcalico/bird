/*
 *	BIRD Internet Routing Daemon -- Unix Entry Point
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/signal.h>

#include "nest/bird.h"
#include "lib/lists.h"
#include "lib/resource.h"
#include "lib/socket.h"
#include "lib/event.h"
#include "nest/route.h"
#include "nest/protocol.h"
#include "nest/iface.h"
#include "nest/cli.h"
#include "conf/conf.h"
#include "filter/filter.h"

#include "unix.h"
#include "krt.h"

int shutting_down;

/*
 *	Debugging
 */

void
async_dump(void)
{
  debug("INTERNAL STATE DUMP\n\n");

  rdump(&root_pool);
  sk_dump_all();
  tm_dump_all();
  if_dump_all();
  neigh_dump_all();
  rta_dump_all();
  rt_dump_all();
  protos_dump_all();

  debug("\n");
}

/*
 *	Reading the Configuration
 */

static int conf_fd;
static char *config_name = PATH_CONFIG;

static int
cf_read(byte *dest, unsigned int len)
{
  int l = read(conf_fd, dest, len);
  if (l < 0)
    cf_error("Read error");
  return l;
}

void
sysdep_preconfig(struct config *c)
{
  init_list(&c->logfiles);
}

void
sysdep_commit(struct config *c)
{
  log_switch(&c->logfiles);
}

static void
read_config(void)
{
  struct config *conf = config_alloc(config_name);

  conf_fd = open(config_name, O_RDONLY);
  if (conf_fd < 0)
    die("Unable to open configuration file %s: %m", config_name);
  cf_read_hook = cf_read;
  if (!config_parse(conf))
    die("%s, line %d: %s", config_name, conf->err_lino, conf->err_msg);
  config_commit(conf);
}

void
async_config(void)
{
  debug("Asynchronous reconfigurations are not supported in demo version\n");
}

/*
 *	Command-Line Interface
 */

static sock *cli_sk;

int
cli_write(cli *c)
{
  sock *s = c->priv;

  if (c->tx_pos)
    {
      struct cli_out *o = c->tx_pos;
      c->tx_pos = o->next;
      s->tbuf = o->outpos;
      return sk_send(s, o->wpos - o->outpos);
    }
  return 1;
}

int
cli_get_command(cli *c)
{
  sock *s = c->priv;
  byte *t = c->rx_aux ? : s->rbuf;
  byte *tend = s->rpos;
  byte *d = c->rx_pos;
  byte *dend = c->rx_buf + CLI_RX_BUF_SIZE - 2;

  while (t < tend)
    {
      if (*t == '\r')
	t++;
      else if (*t == '\n')
	{
	  t++;
	  c->rx_pos = c->rx_buf;
	  c->rx_aux = t;
	  *d = 0;
	  return (d < dend) ? 1 : -1;
	}
      else if (d < dend)
	*d++ = *t++;
    }
  c->rx_aux = s->rpos = s->rbuf;
  c->rx_pos = d;
  return 0;
}

static int
cli_rx(sock *s, int size)
{
  cli_kick(s->data);
  return 0;
}

static void
cli_tx(sock *s)
{
  cli *c = s->data;

  if (cli_write(c))
    cli_written(c);
}

static void
cli_err(sock *s, int err)
{
  if (err)
    log(L_INFO "CLI connection dropped: %s", strerror(err));
  else
    log(L_INFO "CLI connection closed");
  s->type = SK_DELETED;
  cli_free(s->data);
}

static int
cli_connect(sock *s, int size)
{
  cli *c;

  log(L_INFO "CLI connect");
  s->rx_hook = cli_rx;
  s->tx_hook = cli_tx;
  s->err_hook = cli_err;
  s->rbsize = 1024;
  s->data = c = cli_new(s);
  c->rx_pos = c->rx_buf;
  c->rx_aux = NULL;
  return 1;
}

static void
cli_init_unix(void)
{
  sock *s;

  cli_init();
  s = cli_sk = sk_new(cli_pool);
  s->type = SK_UNIX_PASSIVE;
  s->rx_hook = cli_connect;
  sk_open_unix(s, PATH_CONTROL_SOCKET);
}

/*
 *	Shutdown
 */

void
async_shutdown(void)
{
  debug("Shutting down...\n");
  shutting_down = 1;
  protos_shutdown();
}

void
protos_shutdown_notify(void)
{
  unlink(PATH_CONTROL_SOCKET);
  die("System shutdown completed");
}

/*
 *	Signals
 */

static void
handle_sighup(int sig)
{
  debug("Caught SIGHUP...\n");
  async_config_flag = 1;
}

static void
handle_sigusr(int sig)
{
  debug("Caught SIGUSR...\n");
  async_dump_flag = 1;
}

static void
handle_sigterm(int sig)
{
  debug("Caught SIGTERM...\n");
  async_shutdown_flag = 1;
}

static void
signal_init(void)
{
  struct sigaction sa;

  bzero(&sa, sizeof(sa));
  sa.sa_handler = handle_sigusr;
  sa.sa_flags = SA_RESTART;
  sigaction(SIGUSR1, &sa, NULL);
  sa.sa_handler = handle_sighup;
  sa.sa_flags = SA_RESTART;
  sigaction(SIGHUP, &sa, NULL);
  sa.sa_handler = handle_sigterm;
  sa.sa_flags = SA_RESTART;
  sigaction(SIGTERM, &sa, NULL);
  signal(SIGPIPE, SIG_IGN);
}

/*
 *	Parsing of command-line arguments
 */

static char *opt_list = "c:dD:";
static int debug_flag = 1;		/* FIXME: Turn off for production use */

static void
usage(void)
{
  fprintf(stderr, "Usage: bird [-c <config-file>] [-d] [-D <debug-file>]\n");
  exit(1);
}

static void
parse_args(int argc, char **argv)
{
  int c;

  while ((c = getopt(argc, argv, opt_list)) >= 0)
    switch (c)
      {
      case 'c':
	config_name = optarg;
	break;
      case 'd':
	debug_flag |= 1;
	break;
      case 'D':
	log_init_debug(optarg);
	debug_flag |= 2;
	break;
      default:
	usage();
      }
  if (optind < argc)
    usage();
}

/*
 *	Hic Est main()
 */

int
main(int argc, char **argv)
{
#ifdef HAVE_LIBDMALLOC
  if (!getenv("DMALLOC_OPTIONS"))
    dmalloc_debug(0x2f03d00);
#endif

  setvbuf(stdout, NULL, _IONBF, 0);	/* FIXME: Kill some day. */
  setvbuf(stderr, NULL, _IONBF, 0);
  parse_args(argc, argv);
  if (debug_flag == 1)
    log_init_debug("");
  log_init(debug_flag);

  log(L_INFO "Launching BIRD " BIRD_VERSION "...");

  debug("Initializing.\n");
  resource_init();
  io_init();
  rt_init();
  if_init();

  protos_build();
  add_tail(&protocol_list, &proto_unix_kernel.n);
  add_tail(&protocol_list, &proto_unix_iface.n);

  read_config();

  signal_init();

  cli_init_unix();

  protos_start();

  ev_run_list(&global_event_list);
  async_dump();

  debug("Entering I/O loop.\n");

  io_loop();
  bug("I/O loop died");
}
