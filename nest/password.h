/*
 *	BIRD -- Password handling
 *
 *	Copyright 1999 Pavel Machek <pavel@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef PASSWORD_H
#define PASSWORD_H
struct password_item {
  struct password_item *next;
  char *password;
  int id;
  unsigned int from, to;	/* We really don't care about time before 1970 */
};

extern struct password_item *last_password_item;
#endif
