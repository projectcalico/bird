/*
 *	BIRD -- Configuration Lexer
 *
 *	(c) 1998--1999 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

%{
#undef REJECT     /* Avoid name clashes */

#include "filter/filter.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "nest/bird.h"
#include "conf/conf.h"
#include "conf/cf-parse.tab.h"

static struct keyword {
  byte *name;
  int value;
  struct keyword *next;
} keyword_list[] = {
#include "conf/keywords.h"
  { NULL, -1 } };

#define KW_HASH_SIZE 64
#define SYM_HASH_SIZE 128
#define SYM_MAX_LEN 32

static struct keyword *kw_hash[KW_HASH_SIZE];
static struct symbol **sym_hash;

int conf_lino;

static int cf_hash(byte *c);
static struct symbol *cf_find_sym(byte *c, unsigned int h0);

linpool *cfg_mem;

int (*cf_read_hook)(byte *buf, unsigned int max);

#define YY_INPUT(buf,result,max) result = cf_read_hook(buf, max);
#define YY_NO_UNPUT
#define YY_FATAL_ERROR(msg) cf_error(msg)

%}

%option noyywrap

%x COMMENT CCOMM CLI

ALPHA [a-zA-Z_]
DIGIT [0-9]
XIGIT [0-9a-fA-F]
ALNUM [a-zA-Z_0-9]
WHITE [ \t]

%%

{DIGIT}+\.{DIGIT}+\.{DIGIT}+\.{DIGIT}+ {
#ifdef IPV6
  if (ipv4_pton_u32(yytext, &cf_lval.i32))
    return RTRID;
  cf_error("Invalid IPv4 address %s", yytext);
#else
  if (ip_pton(yytext, &cf_lval.a))
    return IPA;
  cf_error("Invalid IP address %s", yytext);
#endif
}

({XIGIT}*::|({XIGIT}*:){3,})({XIGIT}*|{DIGIT}+\.{DIGIT}+\.{DIGIT}+\.{DIGIT}+) {
#ifdef IPV6
  if (ip_pton(yytext, &cf_lval.a))
    return IPA;
  cf_error("Invalid IP address %s", yytext);
#else
  cf_error("This is an IPv4 router, therefore IPv6 addresses are not supported");
#endif
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

<CLI>! {
  BEGIN(INITIAL);
  return CLI_MARKER;
}

[={}:;,()+*/%-<>~\[\]] {
  return yytext[0];
}

["][^"\n]*["] {
  cf_lval.t = yytext+1;
  yytext[yyleng-1] = 0;
  return TEXT;
}

["][^"\n]*\n	cf_error("Unterminated string");

<INITIAL,COMMENT><<EOF>>	return END;

{WHITE}+

\n	conf_lino++;

#	BEGIN(COMMENT);

\/\*	BEGIN(CCOMM);

.	cf_error("Unknown character");

<COMMENT>\n {
  conf_lino++;
  BEGIN(INITIAL);
}

<COMMENT>.

<CCOMM>\*\/	BEGIN(INITIAL);
<CCOMM>\n	conf_lino++;
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
  struct symbol *s;
  int l;

  if (!sym_hash)
    sym_hash = cfg_allocz(SYM_HASH_SIZE * sizeof(struct keyword *));
  else
    for(s = sym_hash[h]; s; s=s->next)
      {
	if (!strcmp(s->name, c))
	  return s;
	s = s->next;
      }
  l = strlen(c);
  if (l > SYM_MAX_LEN)
    cf_error("Symbol too long");
  s = cfg_alloc(sizeof(struct symbol) + l);
  s->next = sym_hash[h];
  sym_hash[h] = s;
  s->class = SYM_VOID;
  s->def = NULL;
  s->aux = 0;
  strcpy(s->name, c);
  return s;
}

struct symbol *
cf_find_symbol(byte *c)
{
  return cf_find_sym(c, cf_hash(c));
}

struct symbol *
cf_default_name(char *prefix, int *counter)
{
  char buf[32];
  struct symbol *s;

  do
    {
      sprintf(buf, "%s%d", prefix, ++(*counter));
      s = cf_find_sym(buf, cf_hash(buf));
      if (!s) cf_error("Unable to generate default name");
    }
  while (s->class != SYM_VOID);
  return s;
}

void
cf_define_symbol(struct symbol *sym, int type, void *def)
{
  if (sym->class)
    cf_error("Symbol already defined");
  sym->class = type;
  sym->def = def;
}

void
cf_lex_init(int is_cli)
{
  sym_hash = NULL;
  conf_lino = 1;
  yyrestart(NULL);
  if (is_cli)
    BEGIN(CLI);
  else
    BEGIN(INITIAL);
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
