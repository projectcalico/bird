/*
 *	BIRD Library
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_BIRDLIB_H_
#define _BIRD_BIRDLIB_H_

/* Ugly structure offset handling macros */

#define OFFSETOF(s, i) ((unsigned int)&((s *)0)->i)
#define SKIP_BACK(s, i, p) ((s *)((char *)p - OFFSETOF(s, i)))
#define ALIGN(s, a) (((s)+a-1)&~(a-1))

/* Utility-Macros */
#define MIN(a,b) ((a<b)?a:b)
#define MAX(a,b) ((a>b)?a:b)

/* Functions which don't return */

#define NORET __attribute__((noreturn))

/* Logging and dying */

void log(char *msg, ...) __attribute__((format(printf,1,2)));
void die(char *msg, ...) __attribute__((format(printf,1,2))) NORET;

#define L_DEBUG "\001"			/* Debugging messages */
#define L_INFO "\002"			/* Informational messages */
#define L_WARN "\003"			/* Warnings */
#define L_ERR "\004"			/* Errors */
#define L_AUTH "\005"			/* Authorization failed etc. */
#define L_FATAL "\006"			/* Fatal errors */

void log_init(char *);			/* Initialize logging to given file (NULL=stderr, ""=syslog) */
void log_init_debug(char *);		/* Initialize debug dump to given file (NULL=stderr, ""=off) */

void debug(char *msg, ...);		/* Printf to debug output */

/* Debugging */

#ifdef LOCAL_DEBUG
#define DBG(x, y...) debug(x, ##y)
#else
#define DBG(x, y...)
#endif

#endif
