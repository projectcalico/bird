/*
 *	BIRD Internet Routing Daemon -- Configuration File Handling
 *
 *	(c) 1998--2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <setjmp.h>
#include <stdarg.h>

#undef LOCAL_DEBUG

#include "nest/bird.h"
#include "nest/route.h"
#include "nest/protocol.h"
#include "nest/iface.h"
#include "lib/resource.h"
#include "lib/string.h"
#include "lib/event.h"
#include "lib/timer.h"
#include "conf/conf.h"
#include "filter/filter.h"

static jmp_buf conf_jmpbuf;

struct config *config, *new_config, *old_config, *future_config;
static event *config_event;
int shutting_down;
bird_clock_t boot_time;

struct config *
config_alloc(byte *name)
{
  pool *p = rp_new(&root_pool, "Config");
  linpool *l = lp_new(p, 4080);
  struct config *c = lp_allocz(l, sizeof(struct config));

  c->pool = p;
  cfg_mem = c->mem = l;
  c->file_name = cfg_strdup(name);
  c->load_time = now;
  if (!boot_time)
    boot_time = now;
  return c;
}

int
config_parse(struct config *c)
{
  DBG("Parsing configuration file `%s'\n", c->file_name);
  new_config = c;
  cfg_mem = c->mem;
  if (setjmp(conf_jmpbuf))
    return 0;
  cf_lex_init(0);
  sysdep_preconfig(c);
  protos_preconfig(c);
  rt_preconfig(c);
  cf_parse();
  filters_postconfig();			/* FIXME: Do we really need this? */
  protos_postconfig(c);
#ifdef IPV6
  if (!c->router_id)
    cf_error("Router ID must be configured manually on IPv6 routers");
#endif
  return 1;
}

int
cli_parse(struct config *c)
{
  new_config = c;
  c->sym_fallback = config->sym_hash;
  cfg_mem = c->mem;
  if (setjmp(conf_jmpbuf))
    return 0;
  cf_lex_init(1);
  cf_parse();
  return 1;
}

void
config_free(struct config *c)
{
  rfree(c->pool);
}

void
config_add_obstacle(struct config *c)
{
  DBG("+++ adding obstacle %d\n", c->obstacle_count);
  c->obstacle_count++;
}

void
config_del_obstacle(struct config *c)
{
  DBG("+++ deleting obstacle %d\n", c->obstacle_count);
  c->obstacle_count--;
  if (!c->obstacle_count)
    {
      ASSERT(config_event);
      ev_schedule(config_event);
    }
}

static int
global_commit(struct config *new, struct config *old)
{
  if (!old)
    return 0;
  if (!new->router_id)
    new->router_id = old->router_id;
  if (new->router_id != old->router_id)
    return 1;
  return 0;
}

static int
config_do_commit(struct config *c)
{
  int force_restart, nobs;

  DBG("do_commit\n");
  old_config = config;
  config = new_config = c;
  if (old_config)
    old_config->obstacle_count++;
  DBG("sysdep_commit\n");
  force_restart = sysdep_commit(c, old_config);
  DBG("global_commit\n");
  force_restart |= global_commit(c, old_config);
  DBG("rt_commit\n");
  rt_commit(c, old_config);
  DBG("protos_commit\n");
  protos_commit(c, old_config, force_restart);
  new_config = NULL;			/* Just to be sure nobody uses that now */
  if (old_config)
    nobs = --old_config->obstacle_count;
  else
    nobs = 0;
  DBG("do_commit finished with %d obstacles remaining\n", nobs);
  return !nobs;
}

static int
config_done(void *unused)
{
  struct config *c;

  DBG("config_done\n");
  for(;;)
    {
      if (config->shutdown)
	sysdep_shutdown_done();
      log(L_INFO "Reconfigured");
      if (old_config)
	{
	  config_free(old_config);
	  old_config = NULL;
	}
      if (!future_config)
	break;
      c = future_config;
      future_config = NULL;
      log(L_INFO "Switching to queued configuration...");
      if (!config_do_commit(c))
	break;
    }
  return 0;
}

int
config_commit(struct config *c)
{
  if (!config)				/* First-time configuration */
    {
      config_do_commit(c);
      return CONF_DONE;
    }
  if (old_config)			/* Reconfiguration already in progress */
    {
      if (shutting_down)
	{
	  log(L_INFO "New configuration discarded due to shutdown");
	  config_free(c);
	  return CONF_SHUTDOWN;
	}
      if (future_config)
	{
	  log(L_INFO "Queueing new configuration, ignoring the one already queued");
	  config_free(future_config);
	}
      else
	log(L_INFO "Queued new configuration");
      future_config = c;
      return CONF_QUEUED;
    }
  if (config_do_commit(c))
    {
      config_done(NULL);
      return CONF_DONE;
    }
  if (!config_event)
    {
      config_event = ev_new(&root_pool);
      config_event->hook = config_done;
    }
  return CONF_PROGRESS;
}

void
order_shutdown(void)
{
  struct config *c;

  if (shutting_down)
    return;
  log(L_INFO "Shutting down");
  c = lp_alloc(config->mem, sizeof(struct config));
  memcpy(c, config, sizeof(struct config));
  init_list(&c->protos);
  init_list(&c->tables);
  c->shutdown = 1;
  config_commit(c);
  shutting_down = 1;
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
