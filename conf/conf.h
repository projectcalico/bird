/*
 *	BIRD Internet Routing Daemon -- Configuration File Handling
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_CONF_H_
#define _BIRD_CONF_H_

#include "lib/resource.h"

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
#define SYM_VARIABLE_INT 4
#define SYM_FUNCTION 5
#define SYM_FILTER 6

void cf_lex_init_tables(void);
int cf_lex(void);
void cf_lex_init(int flag);
void cf_error(char *msg, ...) NORET;
void cf_allocate(void);
struct symbol *cf_default_name(char *prefix);

/* Parser */

int cf_parse(void);

#endif
