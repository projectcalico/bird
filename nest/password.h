/*
 *	BIRD -- Password handling
 *
 *	Copyright 1999 Pavel Machek <pavel@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef PASSWORD_H
#define PASSWORD_H
#include "lib/timer.h"

struct password_item {
  struct password_item *next;
  char *password;
  int id;
  bird_clock_t from, passive, to;
};

extern struct password_item *last_password_item;

struct password_item *get_best_password(struct password_item *head, int flags);
extern void password_strncpy(char *to, char *from, int len);


#endif
