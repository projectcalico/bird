/*
 *	BIRD Client -- Command Handling
 *
 *	(c) 1999--2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>

#include "nest/bird.h"
#include "lib/resource.h"
#include "client/client.h"

struct cmd_info {
  char *command;
  char *args;
  char *help;
  int is_real_cmd;
};

static struct cmd_info command_table[] = {
#include "conf/commands.h"
};

/* FIXME: There should exist some system of aliases, so that `show' can be abbreviated as `s' etc. */

struct cmd_node {
  struct cmd_node *sibling, *son, **plastson;
  struct cmd_info *cmd;
  int len;
  char token[1];
};

static struct cmd_node cmd_root;

void
cmd_build_tree(void)
{
  unsigned int i;

  cmd_root.plastson = &cmd_root.son;

  for(i=0; i<sizeof(command_table) / sizeof(struct cmd_info); i++)
    {
      struct cmd_info *cmd = &command_table[i];
      struct cmd_node *old, *new;
      char *c = cmd->command;

      old = &cmd_root;
      while (*c)
	{
	  char *d = c;
	  while (*c && *c != ' ')
	    c++;
	  for(new=old->son; new; new=new->sibling)
	    if (new->len == c-d && !memcmp(new->token, d, c-d))
	      break;
	  if (!new)
	    {
	      new = xmalloc(sizeof(struct cmd_node) + c-d);
	      *old->plastson = new;
	      old->plastson = &new->sibling;
	      new->sibling = new->son = NULL;
	      new->plastson = &new->son;
	      new->cmd = NULL;
	      new->len = c-d;
	      memcpy(new->token, d, c-d);
	      new->token[c-d] = 0;
	    }
	  old = new;
	  while (*c == ' ')
	    c++;
	}
      old->cmd = cmd;
    }
}

static void
cmd_display_help(struct cmd_info *c)
{
  char buf[strlen(c->command) + strlen(c->args) + 4];

  sprintf(buf, "%s %s", c->command, c->args);
  printf("%-45s  %s\n", buf, c->help);
}

static struct cmd_node *
cmd_find_abbrev(struct cmd_node *root, char *cmd, int len)
{
  struct cmd_node *m, *best = NULL, *best2 = NULL;

  for(m=root->son; m; m=m->sibling)
    {
      if (m->len == len && !memcmp(m->token, cmd, len))
	return m;
      if (m->len > len && !memcmp(m->token, cmd, len))
	{
	  best2 = best;
	  best = m;
	}
    }
  return best2 ? NULL : best;
}

void
cmd_help(char *cmd, int len)
{
  char *end = cmd + len;
  struct cmd_node *n, *m;
  char *z;

  n = &cmd_root;
  while (cmd < end)
    {
      if (*cmd == ' ' || *cmd == '\t')
	{
	  cmd++;
	  continue;
	}
      z = cmd;
      while (cmd < end && *cmd != ' ' && *cmd != '\t')
	cmd++;
      m = cmd_find_abbrev(n, z, cmd-z);
      if (!m)
	break;
      n = m;
    }
  if (n->cmd && n->cmd->is_real_cmd)
    cmd_display_help(n->cmd);
  for (m=n->son; m; m=m->sibling)
    cmd_display_help(m->cmd);
}
