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

extern pool *cfg_pool;
extern mempool *cfg_mem;

/* Lexer */

extern int (*cf_read_hook)(byte *buf, unsigned int max);

struct symbol {
  struct symbol *next;
  int class;
  void *def;
  char name[1];
};

#define SYM_VOID 0

void cf_lex_init_tables(void);
int cf_lex(void);
void cf_lex_init(int flag);
void cf_error(char *msg) NORET;
void cf_allocate(void);

/* Parser */

int cf_parse(void);

#endif
