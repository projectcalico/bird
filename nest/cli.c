/*
 *	BIRD Internet Routing Daemon -- Command-Line Interface
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "nest/bird.h"
#include "lib/string.h"
#include "nest/cli.h"

pool *cli_pool;

void
cli_printf(cli *c, int code, char *msg, ...)
{
  va_list args;
  byte buf[1024];
  int flag = (code < 0) ? '-' : ' ';
  int size;
  struct cli_out *o;

  va_start(args, msg);
  if (code < 0)
    code = -code;
  bsprintf(buf, "%04d%c", code, flag);
  size = bvsnprintf(buf+5, sizeof(buf)-6, msg, args);
  if (size < 0)
    size = bsprintf(buf, "9999%c<line overflow>", flag);
  else
    size += 5;
  buf[size++] = '\n';
  if (!(o = c->tx_write) || o->wpos + size > o->end)
    {
      o = mb_alloc(c->pool, sizeof(struct cli_out) + CLI_TX_BUF_SIZE);
      if (c->tx_write)
	c->tx_write->next = o;
      else
	c->tx_buf = o;
      o->next = NULL;
      o->wpos = o->outpos = o->buf;
      o->end = o->buf + CLI_TX_BUF_SIZE;
      c->tx_write = o;
    }
  memcpy(o->wpos, buf, size);
  o->wpos += size;
}

static void
cli_free_out(cli *c)
{
  struct cli_out *o, *p;

  if (o = c->tx_buf)
    {
      c->tx_write = o;
      o->wpos = o->outpos = o->buf;
      while (p = o->next)
	{
	  o->next = p->next;
	  mb_free(p);
	}
    }
}

static int
cli_flush(cli *c)
{
  if (cli_write(c))
    {
      cli_free_out(c);
      return 1;
    }
  return 0;
}

static int
cli_event(void *data)
{
  cli *c = data;
  int err;

  debug("CLI EVENT\n");
  if (!c->inited)
    {
      c->inited = 1;
      cli_printf(c, 0, "Welcome!");
      cli_printf(c, 0, "Here");
      return cli_flush(c);
    }
  err = cli_get_command(c);
  if (!err)
    return 0;
  if (err < 0)
    debug("CLI CMD ERR\n");
  else
    debug("CLI CMD %s\n", c->rx_buf);
  return 1;
}

cli *
cli_new(void *priv)
{
  pool *p = rp_new(cli_pool, "CLI");
  cli *c = mb_alloc(p, sizeof(cli));

  c->pool = p;
  c->priv = priv;
  c->event = ev_new(p);
  c->event->hook = cli_event;
  c->event->data = c;
  c->tx_buf = c->tx_pos = c->tx_write = NULL;
  c->inited = 0;
  cli_kick(c);
  return c;
}

void
cli_kick(cli *c)
{
  debug("CLI KICK\n");
  ev_schedule(c->event);
}

void
cli_written(cli *c)
{
  debug("CLI WRITTEN\n");
  cli_free_out(c);
  cli_kick(c);
}

void
cli_free(cli *c)
{
  rfree(c->pool);
}

void
cli_init(void)
{
  cli_pool = rp_new(&root_pool, "CLI");
}
