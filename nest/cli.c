/*
 *	BIRD Internet Routing Daemon -- Command-Line Interface
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "nest/bird.h"
#include "nest/cli.h"
#include "conf/conf.h"
#include "lib/string.h"

pool *cli_pool;

static byte *
cli_alloc_out(cli *c, int size)
{
  struct cli_out *o;

  if (!(o = c->tx_write) || o->wpos + size > o->end)
    {
      if (!o && c->tx_buf)
	o = c->tx_buf;
      else
	{
	  o = mb_alloc(c->pool, sizeof(struct cli_out) + CLI_TX_BUF_SIZE);
	  if (c->tx_write)
	    c->tx_write->next = o;
	  else
	    c->tx_buf = o;
	  o->wpos = o->outpos = o->buf;
	  o->end = o->buf + CLI_TX_BUF_SIZE;
	}
      c->tx_write = o;
      if (!c->tx_pos)
	c->tx_pos = o;
      o->next = NULL;
    }
  o->wpos += size;
  return o->wpos - size;
}

void
cli_printf(cli *c, int code, char *msg, ...)
{
  va_list args;
  byte buf[1024];
  int cd = code;
  int size, cnt;

  va_start(args, msg);
  if (cd < 0)
    {
      cd = -cd;
      if (cd == c->last_reply)
	size = bsprintf(buf, " ");
      else
	size = bsprintf(buf, "%04d-", cd);
    }
  else
    size = bsprintf(buf, "%04d ", cd);
  c->last_reply = cd;
  cnt = bvsnprintf(buf+size, sizeof(buf)-size-1, msg, args);
  if (cnt < 0)
    {
      cli_printf(c, code < 0 ? -8000 : 8000, "<line overflow>");
      return;
    }
  size += cnt;
  buf[size++] = '\n';
  memcpy(cli_alloc_out(c, size), buf, size);
}

static void
cli_copy_message(cli *c)
{
  byte *p, *q;
  unsigned int cnt = 2;

  if (c->ring_overflow)
    {
      byte buf[64];
      int n = bsprintf(buf, "<%d messages lost>\n", c->ring_overflow);
      c->ring_overflow = 0;
      memcpy(cli_alloc_out(c, n), buf, n);
    }
  p = c->ring_read;
  while (*p)
    {
      cnt++;
      p++;
      if (p == c->ring_end)
	p = c->ring_buf;
      ASSERT(p != c->ring_write);
    }
  c->async_msg_size += cnt;
  q = cli_alloc_out(c, cnt);
  *q++ = '+';
  p = c->ring_read;
  do
    {
      *q = *p++;
      if (p == c->ring_end)
	p = c->ring_buf;
    }
  while (*q++);
  c->ring_read = p;
  q[-1] = '\n';
}

static void
cli_hello(cli *c)
{
  cli_printf(c, 1, "BIRD " BIRD_VERSION " ready.");
  c->cont = NULL;
}

static void
cli_free_out(cli *c)
{
  struct cli_out *o, *p;

  if (o = c->tx_buf)
    {
      o->wpos = o->outpos = o->buf;
      while (p = o->next)
	{
	  o->next = p->next;
	  mb_free(p);
	}
    }
  c->tx_write = c->tx_pos = NULL;
  c->async_msg_size = 0;
}

static byte *cli_rh_pos;
static unsigned int cli_rh_len;
static int cli_rh_trick_flag;
struct cli *this_cli;

static int
cli_cmd_read_hook(byte *buf, unsigned int max)
{
  if (!cli_rh_trick_flag)
    {
      cli_rh_trick_flag = 1;
      buf[0] = '!';
      return 1;
    }
  if (max > cli_rh_len)
    max = cli_rh_len;
  memcpy(buf, cli_rh_pos, max);
  cli_rh_pos += max;
  cli_rh_len -= max;
  return max;
}

static void
cli_command(struct cli *c)
{
  struct config f;
  int res;

  bzero(&f, sizeof(f));
  f.mem = c->parser_pool;
  cf_read_hook = cli_cmd_read_hook;
  cli_rh_pos = c->rx_buf;
  cli_rh_len = strlen(c->rx_buf);
  cli_rh_trick_flag = 0;
  this_cli = c;
  lp_flush(c->parser_pool);
  res = cli_parse(&f);
  if (!res)
    cli_printf(c, 9001, f.err_msg);
}

static void
cli_event(void *data)
{
  cli *c = data;
  int err;

  while (c->ring_read != c->ring_write &&
      c->async_msg_size < CLI_MAX_ASYNC_QUEUE)
    cli_copy_message(c);

  if (c->tx_pos)
    ;
  else if (c->cont)
    c->cont(c);
  else
    {
      err = cli_get_command(c);
      if (!err)
	return;
      if (err < 0)
	cli_printf(c, 9000, "Command too long");
      else
	cli_command(c);
    }
  if (cli_write(c))
    {
      cli_free_out(c);
      ev_schedule(c->event);
    }
}

cli *
cli_new(void *priv)
{
  pool *p = rp_new(cli_pool, "CLI");
  cli *c = mb_alloc(p, sizeof(cli));

  bzero(c, sizeof(cli));
  c->pool = p;
  c->priv = priv;
  c->event = ev_new(p);
  c->event->hook = cli_event;
  c->event->data = c;
  c->cont = cli_hello;
  c->parser_pool = lp_new(c->pool, 4096);
  c->rx_buf = mb_alloc(c->pool, CLI_RX_BUF_SIZE);
  ev_schedule(c->event);
  return c;
}

void
cli_kick(cli *c)
{
  if (!c->cont && !c->tx_pos)
    ev_schedule(c->event);
}

void
cli_written(cli *c)
{
  cli_free_out(c);
  ev_schedule(c->event);
}

static list cli_log_hooks;
static int cli_log_inited;

void
cli_set_log_echo(cli *c, unsigned int mask, unsigned int size)
{
  if (c->ring_buf)
    {
      mb_free(c->ring_buf);
      c->ring_buf = c->ring_end = c->ring_read = c->ring_write = NULL;
      rem_node(&c->n);
    }
  c->log_mask = mask;
  if (mask && size)
    {
      c->ring_buf = mb_alloc(c->pool, size);
      c->ring_end = c->ring_buf + size;
      c->ring_read = c->ring_write = c->ring_buf;
      add_tail(&cli_log_hooks, &c->n);
      c->log_threshold = size / 8;
    }
  c->ring_overflow = 0;
}

void
cli_echo(unsigned int class, byte *msg)
{
  unsigned len, free, i, l;
  cli *c;
  byte *m;

  if (!cli_log_inited || EMPTY_LIST(cli_log_hooks))
    return;
  len = strlen(msg) + 1;
  WALK_LIST(c, cli_log_hooks)
    {
      if (!(c->log_mask & (1 << class)))
	continue;
      if (c->ring_read <= c->ring_write)
	free = (c->ring_end - c->ring_buf) - (c->ring_write - c->ring_read + 1);
      else
	free = c->ring_read - c->ring_write - 1;
      if (len > free ||
	  free < c->log_threshold && class < (unsigned) L_INFO[0])
	{
	  c->ring_overflow++;
	  continue;
	}
      if (c->ring_read == c->ring_write)
	ev_schedule(c->event);
      m = msg;
      l = len;
      while (l)
	{
	  if (c->ring_read <= c->ring_write)
	    i = c->ring_end - c->ring_write;
	  else
	    i = c->ring_read - c->ring_write;
	  if (i > l)
	    i = l;
	  memcpy(c->ring_write, m, i);
	  m += i;
	  l -= i;
	  c->ring_write += i;
	  if (c->ring_write == c->ring_end)
	    c->ring_write = c->ring_buf;
	}
    }
}

void
cli_free(cli *c)
{
  cli_set_log_echo(c, 0, 0);
  if (c->cleanup)
    c->cleanup(c);
  rfree(c->pool);
}

void
cli_init(void)
{
  cli_pool = rp_new(&root_pool, "CLI");
  init_list(&cli_log_hooks);
  cli_log_inited = 1;
}
