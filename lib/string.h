/*
 *	BIRD Library -- String Functions
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_STRING_H_
#define _BIRD_STRING_H_

#include <stdarg.h>

int bsprintf(char *str, const char *fmt, ...);
int bvsprintf(char *str, const char *fmt, va_list args);

#endif
