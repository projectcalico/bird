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

#include "nest/bird.h"
#include "lib/lists.h"
#include "lib/resource.h"
#include "lib/socket.h"
#include "nest/route.h"
#include "nest/protocol.h"
#include "nest/iface.h"
#include "conf/conf.h"
#include "filter/filter.h"

struct f_inst *last_func = NULL;

#define runtime die

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
    interpret(what->arg1);
    interpret(what->arg2);
    break;
  case '+':
    v1 = interpret(what->arg1);
    v2 = interpret(what->arg2);
    if (v1.type != v2.type)
      runtime( "Can not operate with values of incompatible types" );

    switch (res.type = v1.type) {
    case T_VOID: runtime( "Can not operate with values of type void" );
    case T_INT: res.val.i = v1.val.i + v2.val.i; break;
    default: runtime( "Usage of unknown type" );
    }
    break;
  case '=':
    v1 = interpret(what->arg2);
    sym = what->arg1;
    switch (res.type = v1.type) {
    case T_VOID: runtime( "Can not assign void values" );
    case T_INT: 
      if (sym->class != SYM_VARIABLE_INT)
	runtime( "Variable of bad type" );
      sym->aux = v1.val.i; 
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
    v1 = interpret(what->arg1);
    printf( "Printing: " );
    switch (v1.type) {
    case T_VOID: printf( "(void)" ); break;
    case T_INT: printf( "%d", v1.val.i ); break;
    default: runtime( "Print of variable of unknown type" );
    }
    printf( "\n" );
    break;
  case '?':
    v1 = interpret(what->arg1);
    if (v1.type != T_INT)
      runtime( "If requires integer expression" );
    if (v1.val.i)
      res = interpret(what->arg2);
    break;
  case 'D':
    printf( "DEBUGGING PRINT\n" );
    break;
  case '0':
    printf( "No operation\n" );
    break;
  case 'd':
    printf( "Puts: %s\n", what->arg1 );
    break;
  case '!':
    die( "Filter asked me to die" );
  default:
    die( "Unknown insruction %d(%c)", what->code, what->code & 0xff);
  }
  if (what->next)
    return interpret(what->next);
  return res;
}

void
filters_postconfig(void)
{
  if (!last_func)
    printf( "No function defined\n" );
  else {
    interpret(last_func);
  }
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
  debug( "Running filter `%s'...", filter->name );

  inst = filter->def;
  interpret(inst);
  debug( "done\n" );
  return F_ACCEPT;
}
