/*
 *	BIRD Internet Routing Daemon -- CLI Commands Which Don't Fit Anywhere Else
 *
 *	(c) 2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "nest/bird.h"
#include "nest/cli.h"
#include "conf/conf.h"
#include "nest/cmds.h"
#include "lib/string.h"

void
cmd_show_status(void)
{
  cli_msg(1000, "BIRD " BIRD_VERSION);
  /* FIXME: Should include uptime, shutdown flag et cetera */
}

void
cmd_show_symbols(struct symbol *sym)
{
  int pos = 0;

  if (sym)
    cli_msg(1010, "%s\t%s", sym->name, cf_symbol_class_name(sym));
  else
    {
      while (sym = cf_walk_symbols(config, sym, &pos))
	cli_msg(-1010, "%s\t%s", sym->name, cf_symbol_class_name(sym));
      cli_msg(0, "");
    }
}
