/*
 *	BIRD -- Password handling
 *
 *	Copyright 1999 Pavel Machek <pavel@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "nest/bird.h"
#include "nest/password.h"

struct password_item *last_password_item = NULL;
