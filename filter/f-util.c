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
    return 0;
  switch(what->code) {
  case ',':
    interpret(what->arg1);
    interpret(what->arg2);
    break;
  case '=':
    sym = what->arg1;
    sym->aux = what->arg2;
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
  }
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

