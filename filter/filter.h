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
  int code;
  void *arg1, *arg2;
};

#endif
