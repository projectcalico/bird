/*
 *	Filters: utility functions
 *
 *	Copyright 1998 Pavel Machek <pavel@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/signal.h>
#include <setjmp.h>

#include "nest/bird.h"
#include "lib/lists.h"
#include "lib/resource.h"
#include "lib/socket.h"
#include "nest/route.h"
#include "nest/protocol.h"
#include "nest/iface.h"
#include "conf/conf.h"
#include "filter/filter.h"

struct f_inst *autoexec_func = NULL;

#define runtime(x) do { \
    log( L_ERR, x ); \
    res.type = T_RETURN; \
    res.val.i = F_ERROR; \
    return res; \
  } while(0)

#define ARG(x,y) \
	x = interpret(what->y); \
	if (x.type == T_RETURN) \
		return x;

#define ONEARG ARG(v1, arg1)
#define TWOARGS ARG(v1, arg1) \
		ARG(v2, arg2)

static struct f_val
interpret(struct f_inst *what)
{
  struct symbol *sym;
  struct f_val v1, v2, res;

  res.type = T_VOID;
  if (!what)
    return res;

  switch(what->code) {
  case ',':
    TWOARGS;
    break;
  case '+':
    TWOARGS;
    if (v1.type != v2.type)
      runtime( "Can not operate with values of incompatible types" );

    switch (res.type = v1.type) {
    case T_VOID: runtime( "Can not operate with values of type void" );
    case T_INT: res.val.i = v1.val.i + v2.val.i; break;
    default: runtime( "Usage of unknown type" );
    }
    break;
  case '=':
    ARG(v2, arg2);
    sym = what->arg1;
    switch (res.type = v2.type) {
    case T_VOID: runtime( "Can not assign void values" );
    case T_INT: 
      if (sym->class != SYM_VARIABLE_INT)
	runtime( "Variable of bad type" );
      sym->aux = v2.val.i; 
      break;
    }
    break;
  case 'c':
    res.type = T_INT;
    res.val.i = (int) what->arg1;
    break;
  case 'i':
    res.type = T_INT;
    res.val.i = * ((int *) what->arg1);
    break;
  case 'p':
    ONEARG;
    printf( "Printing: " );
    switch (v1.type) {
    case T_VOID: printf( "(void)" ); break;
    case T_INT: printf( "%d", v1.val.i ); break;
    default: runtime( "Print of variable of unknown type" );
    }
    printf( "\n" );
    break;
  case '?':
    ONEARG;
    if (v1.type != T_INT)
      runtime( "If requires integer expression" );
    if (v1.val.i) {
      ARG(res,arg2);
    }
    break;
  case 'D':
    printf( "DEBUGGING PRINT\n" );
    break;
  case '0':
    printf( "No operation\n" );
    break;
  case 'd':
    printf( "Puts: %s\n", (char *) what->arg1 );
    break;
  case '!':
    switch ((int) what->arg1) {
    case F_QUITBIRD:
      die( "Filter asked me to die" );
    case F_ACCEPT:
      /* Should take care about turning ACCEPT into MODIFY */
    case F_ERROR:
    case F_REJECT:
      res.type = T_RETURN;
      res.val = (int) what->arg1;
      break;
    default:
      bug( "unknown return type: can not happen");
    }
    break;
  default:
    bug( "Unknown instruction %d (%c)", what->code, what->code & 0xff);
  }
  if (what->next)
    return interpret(what->next);
  return res;
}

struct f_inst *
f_new_inst(void)
{
  struct f_inst * ret;
  ret = cfg_alloc(sizeof(struct f_inst));
  ret->code = 0;
  ret->arg1 = ret->arg2 = ret->next = NULL;
  return ret;
}

int
f_run(struct symbol *filter, struct rte *rtein, struct rte **rteout)
{
  struct f_inst *inst;
  struct f_val res;
  debug( "Running filter `%s'...", filter->name );

  inst = filter->def;
  res = interpret(inst);
  if (res.type != T_RETURN)
    return F_ERROR;
  debug( "done (%d)\n", res.val.i );
  return res.val.i;
}

void
filters_postconfig(void)
{
  if (autoexec_func)
    interpret(autoexec_func);
} 
