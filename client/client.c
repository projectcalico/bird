/*
 *	BIRD Client
 *
 *	(c) 1999--2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "nest/bird.h"
#include "lib/resource.h"
#include "client/client.h"

static char *opt_list = "";

static void
usage(void)
{
  fprintf(stderr, "Usage: birdc\n");
  exit(1);
}

static void
parse_args(int argc, char **argv)
{
  int c;

  while ((c = getopt(argc, argv, opt_list)) >= 0)
    switch (c)
      {
      default:
	usage();
      }
  if (optind < argc)
    usage();
}

static char *
get_command(void)
{
  static char *cmd_buffer;

  if (cmd_buffer)
    free(cmd_buffer);
  cmd_buffer = readline("bird> ");
  if (!cmd_buffer)
    exit(0);
  if (cmd_buffer[0])
    add_history(cmd_buffer);
  return cmd_buffer;
}

int
main(int argc, char **argv)
{
#ifdef HAVE_LIBDMALLOC
  if (!getenv("DMALLOC_OPTIONS"))
    dmalloc_debug(0x2f03d00);
#endif

  parse_args(argc, argv);

  for(;;)
    {
      char *c = get_command();
      puts(c);
    }
}
