/*
 *	BIRD Internet Routing Daemon -- Configuration File Handling
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <setjmp.h>
#include <stdarg.h>

#include "nest/bird.h"
#include "nest/protocol.h"
#include "nest/iface.h"
#include "lib/resource.h"
#include "lib/string.h"
#include "conf/conf.h"
#include "filter/filter.h"

static jmp_buf conf_jmpbuf;

struct config *config, *new_config;

struct config *
config_alloc(byte *name)
{
  pool *p = rp_new(&root_pool, "Config");
  linpool *l = lp_new(p, 1024);
  struct config *c = lp_allocz(l, sizeof(struct config));

  c->pool = p;
  cfg_mem = c->mem = l;
  init_list(&c->protos);
  c->file_name = cfg_strdup(name);
  return c;
}

int
config_parse(struct config *c)
{
  struct proto_config *p;

  debug("Parsing configuration file <%s>\n", c->file_name);
  new_config = c;
  cfg_pool = c->pool;
  cfg_mem = c->mem;
  if (setjmp(conf_jmpbuf))
    return 0;
  cf_lex_init(1);
  cf_lex_init_tables();
  protos_preconfig(c);
  cf_parse();
#if 0					/* FIXME: We don't have interface list yet :( */
  if (!c->router_id && !(c->router_id = auto_router_id()))
    cf_error("Cannot determine router ID (no suitable network interface found), please configure it manually");
#endif
  filters_postconfig();			/* FIXME: Do we really need this? */
  protos_postconfig(c);
  return 1;
}

void
config_free(struct config *c)
{
  rfree(c->pool);
}

void
config_commit(struct config *c)
{
  config = c;
  protos_commit(c);
}

void
cf_error(char *msg, ...)
{
  char buf[1024];
  va_list args;

  va_start(args, msg);
  if (bvsnprintf(buf, sizeof(buf), msg, args) < 0)
    strcpy(buf, "<bug: error message too long>");
  new_config->err_msg = cfg_strdup(buf);
  new_config->err_lino = conf_lino;
  longjmp(conf_jmpbuf, 1);
}

char *
cfg_strdup(char *c)
{
  int l = strlen(c) + 1;
  char *z = cfg_allocu(l);
  memcpy(z, c, l);
  return z;
}
