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

struct f_inst *
f_new_inst(void)
{
  struct f_inst * ret;
  ret = cfg_alloc(sizeof(struct f_inst));
  ret->code = ret->aux = 0;
  ret->arg1 = ret->arg2 = ret->next = NULL;
  return ret;
}

struct f_inst *
f_new_dynamic_attr(int code)
{
  struct f_inst *f = f_new_inst();
  f->aux = EAF_TYPE_INT | EAF_INLINE;
  f->a2.i = code;
  return f;
}

char *
filter_name(struct filter *filter)
{
  if (!filter)
    return "ACCEPT";
  else if (filter == FILTER_REJECT)
    return "REJECT";
  else
    return filter->name;
}
