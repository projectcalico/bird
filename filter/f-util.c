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

struct f_instruction *last_func = NULL;

static void
interpret(struct f_instruction *what)
{
  struct symbol *sym;
  if (!what)
    return;
  switch(what->code) {
  case ',':
    interpret(what->arg1);
    interpret(what->arg2);
    break;
  case '=':
    sym = what->arg1;
    sym->aux = (int) what->arg2;
    break;
  case 'p':
    sym = what->arg1;
    switch(sym->class) {
    case SYM_VARIABLE_INT: 
      printf( "Printing: %d\n", sym->aux );
      break;
    default:
      printf( "Unknown type passed to print\n" );
      break;
    }
    break;
  case 'D':
    printf( "DEBUGGING PRINT\n" );
    break;
  case '0':
    printf( "No operation\n" );
    break;
  }
  interpret(what->next);
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

struct f_instruction *
f_new_inst(void)
{
  struct f_instruction * ret;
  ret = cfg_alloc(sizeof(struct f_instruction));
  ret->code = 0;
  ret->arg1 = ret->arg2 = ret->next = NULL;
  return ret;
}

int
f_run(struct symbol *filter, struct rte *rtein, struct rte **rteout)
{
  struct f_instruction *inst;
  debug( "Running filter `%s'...", filter->name );

  inst = filter->def;
  interpret(inst);
  debug( "done\n" );
  return F_ACCEPT;
}
