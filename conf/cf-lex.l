/*
 *	BIRD -- Configuration Lexer
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

%{

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "nest/bird.h"
#include "conf/conf.h"
#include "conf/cf-parse.tab.h"

static struct keyword {
  byte *name;
  int value;
  struct keyword *next;
} keyword_list[] = {
#include "keywords.h"
  { NULL, -1 } };

#define KW_HASH_SIZE 64
#define SYM_HASH_SIZE 128
#define SYM_MAX_LEN 32

static struct keyword *kw_hash[KW_HASH_SIZE];
static struct symbol **sym_hash;
static int allow_new_symbols;
static int cf_lino;

static int cf_hash(byte *c);
static struct symbol *cf_find_sym(byte *c, unsigned int h0);

pool *cfg_pool;
mempool *cfg_mem;

int (*cf_read_hook)(byte *buf, unsigned int max);

#define YY_INPUT(buf,result,max) result = cf_read_hook(buf, max);
#define YY_NO_UNPUT
#define YY_FATAL_ERROR(msg) cf_error(msg)

%}

%option noyywrap

%x COMMENT CCOMM

ALPHA [a-zA-Z_]
DIGIT [0-9]
XIGIT [0-9a-fA-F]
ALNUM [a-zA-Z_0-9]
WHITE [ \t]

%%

{DIGIT}+\.{DIGIT}+\.{DIGIT}+\.{DIGIT}+ {
  if (ip_pton(yytext, &cf_lval.a))
    return IPA;
  cf_error("Invalid IP address");
}

0x{DIGIT}+ {
  char *e;
  long int l;
  errno = 0;
  l = strtoul(yytext+2, &e, 16);
  if (e && *e || errno == ERANGE || (long int)(int) l != l)
    cf_error("Number out of range");
  cf_lval.i = l;
  return NUM;
}

{DIGIT}+ {
  char *e;
  long int l;
  errno = 0;
  l = strtoul(yytext, &e, 10);
  if (e && *e || errno == ERANGE || (long int)(int) l != l)
    cf_error("Number out of range");
  cf_lval.i = l;
  return NUM;
}

{ALPHA}{ALNUM}* {
  unsigned int h = cf_hash(yytext);
  struct keyword *k = kw_hash[h & (KW_HASH_SIZE-1)];
  while (k)
    {
      if (!strcmp(k->name, yytext))
	return k->value;
      k=k->next;
    }
  cf_lval.s = cf_find_sym(yytext, h);
  return SYM;
}

[{}:;,] {
  return yytext[0];
}

["][^"\n]*["] {
  cf_lval.t = yytext+1;
  yytext[yyleng] = 0;
  return TEXT;
}

["][^"\n]*\n	cf_error("Unterminated string");

<INITIAL,COMMENT><<EOF>>	return END;

{WHITE}+

\\\n {
  cf_lino++;
}

\n {
  cf_lino++;
  return ';';
}

#	{ BEGIN(COMMENT); }

\/\*	{ BEGIN(CCOMM); }

.	cf_error("Unknown character");

<COMMENT>\n {
  cf_lino++;
  BEGIN(INITIAL);
}

<COMMENT>.

<CCOMM>\*\/	BEGIN(INITIAL);
<CCOMM>\n	cf_lino++;
<CCOMM>\/\*	cf_error("Comment nesting not supported");
<CCOMM><<EOF>>	cf_error("Unterminated comment");
<CCOMM>.

%%

static int
cf_hash(byte *c)
{
  unsigned int h = 13;

  while (*c)
    h = (h * 37) + *c++;
  return h;
}

static struct symbol *
cf_find_sym(byte *c, unsigned int h0)
{
  unsigned int h = h0 & (SYM_HASH_SIZE-1);
  struct symbol *s = sym_hash[h];
  int l;

  while (s)
    if (!strcmp(s->name, c))
      return s;
  if (!allow_new_symbols)
    return NULL;
  l = strlen(c);
  if (l > SYM_MAX_LEN)
    cf_error("Symbol too long");
  s = mp_alloc(cfg_mem, sizeof(struct symbol) + l);
  s->next = sym_hash[h];
  sym_hash[h] = s;
  s->class = SYM_VOID;
  s->def = NULL;
  strcpy(s->name, c);
  return s;
}

void
cf_lex_init(int flag)
{
  if (allow_new_symbols = flag)
    sym_hash = mp_allocz(cfg_mem, SYM_HASH_SIZE * sizeof(struct keyword *));
  cf_lino = 1;
}

void
cf_lex_init_tables(void)
{
  struct keyword *k;

  for(k=keyword_list; k->name; k++)
    {
      unsigned h = cf_hash(k->name) & (KW_HASH_SIZE-1);
      k->next = kw_hash[h];
      kw_hash[h] = k;
    }
}

void
cf_error(char *msg)
{
  die(PATH_CONFIG ", line %d: %s", cf_lino, msg);
}

void
cf_allocate(void)
{
  if (cfg_pool)
    rfree(cfg_pool);
  cfg_pool = rp_new(&root_pool, "Config");
  cfg_mem = mp_new(cfg_pool, 1024);
}
