/*
 *	BIRD Internet Routing Daemon -- Filters
 *
 *	(c) 1999 Pavel Machek <pavel@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_FILT_H_
#define _BIRD_FILT_H_

#include "lib/resource.h"

struct f_inst {		/* Instruction */
  struct f_inst *next;	/* Structure is 16 bytes, anyway */
  int code;
  void *arg1, *arg2;
};

struct f_val {
  int type;
  union {
    int i;
  } val;
};

struct filter {
  char *name;
  struct f_inst *root;
};

void filters_postconfig(void);
struct f_inst *f_new_inst(void);

int f_run(struct filter *filter, struct rte *rtein, struct rte **rteout);

#define F_ACCEPT 1
#define F_REJECT 2
#define F_MODIFY 3
#define F_ERROR 4
#define F_QUITBIRD 5

#define T_VOID 0
#define T_RETURN 1
#define T_INT 10
#define T_PX 11		/* prefix */
#define T_INTLIST 12

#endif
