/*
 *	BIRD Internet Routing Daemon -- Configuration File Handling
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_CONF_H_
#define _BIRD_CONF_H_

#include "lib/resource.h"

/* Configuration structure */

struct config {
  pool *pool;				/* Pool the configuration is stored in */
  linpool *mem;				/* Linear pool containing configuration data */
  list protos;				/* Configured protocol instances (struct proto_config) */
  list tables;				/* Configured routing tables (struct rtable_config) */
  struct rtable_config *master_rtc;	/* Configuration of master routing table */
  u32 router_id;			/* Our Router ID */
  char *err_msg;			/* Parser error message */
  int err_lino;				/* Line containing error */
  char *file_name;			/* Name of configuration file */
};

extern struct config *config, *new_config;
/* Please don't use these variables in protocols. Use proto_config->global instead. */

struct config *config_alloc(byte *name);
int config_parse(struct config *);
void config_free(struct config *);
void config_commit(struct config *);
void cf_error(char *msg, ...) NORET;

/* Pools */

extern pool *cfg_pool;
extern linpool *cfg_mem;

#define cfg_alloc(size) lp_alloc(cfg_mem, size)
#define cfg_allocu(size) lp_allocu(cfg_mem, size)
#define cfg_allocz(size) lp_allocz(cfg_mem, size)
char *cfg_strdup(char *c);

/* Lexer */

extern int (*cf_read_hook)(byte *buf, unsigned int max);

struct symbol {
  struct symbol *next;
  int class;
  int aux;
  void *def;
  char name[1];
};

#define SYM_VOID 0
#define SYM_PROTO 1
#define SYM_NUMBER 2
#define SYM_STAT 3 /* statement */
#define SYM_FUNCTION 5
#define SYM_FILTER 6
#define SYM_TABLE 7

#define SYM_VARIABLE 0x100	/* Reserved 0x100..0x1ff */

extern int conf_lino;

void cf_lex_init_tables(void);
int cf_lex(void);
void cf_lex_init(int flag);
struct symbol *cf_find_symbol(byte *c);
struct symbol *cf_default_name(char *prefix, int *counter);
void cf_define_symbol(struct symbol *symbol, int type, void *def);

/* Parser */

int cf_parse(void);

#endif
