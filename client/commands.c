/*
 *	BIRD Client -- Command Handling
 *
 *	(c) 1999--2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "nest/bird.h"
#include "client/client.h"

struct cmd_info {
  char *command;
  char *args;
  char *help;
};

struct cmd_info command_table[] = {
#include "conf/commands.h"
};
