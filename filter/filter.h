/*
 *	BIRD Internet Routing Daemon -- Configuration File Handling
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_FILT_H_
#define _BIRD_FILT_H_

#include "lib/resource.h"

/* Lexer */

struct f_instruction {
  struct f_instruction *next;	/* Structure is 16 bytes, anyway */
  int code;
  void *arg1, *arg2;
};

void filters_postconfig(void);
struct f_instruction *f_new_inst(void);

#define F_ACCEPT 1
#define F_REJECT 2
#define F_MODIFY 3

#endif
