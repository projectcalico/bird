/*
 *	BIRD Library
 *
 *	(c) 1998--2000 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_BIRDLIB_H_
#define _BIRD_BIRDLIB_H_

/* Ugly structure offset handling macros */

#define OFFSETOF(s, i) ((unsigned int)&((s *)0)->i)
#define SKIP_BACK(s, i, p) ((s *)((char *)p - OFFSETOF(s, i)))
#define ALIGN(s, a) (((s)+a-1)&~(a-1))

/* Utility macros */

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*(a)))

#ifndef NULL
#define NULL ((void *) 0)
#endif

/* Functions which don't return */

#define NORET __attribute__((noreturn))

/* Logging and dying */

void log(char *msg, ...);
void die(char *msg, ...) NORET;
void bug(char *msg, ...) NORET;

#define L_DEBUG "\001"			/* Debugging messages */
#define L_TRACE "\002"			/* Protocol tracing */
#define L_INFO "\003"			/* Informational messages */
#define L_REMOTE "\004"			/* Remote protocol errors */
#define L_WARN "\004"			/* Local warnings */
#define L_ERR "\005"			/* Local errors */
#define L_AUTH "\006"			/* Authorization failed etc. */
#define L_FATAL "\007"			/* Fatal errors */
#define L_BUG "\010"			/* BIRD bugs */

void debug(char *msg, ...);		/* Printf to debug output */

/* Debugging */

#if defined(LOCAL_DEBUG) || defined(GLOBAL_DEBUG)
#define DBG(x, y...) debug(x, ##y)
#else
#define DBG(x, y...) do { } while(0)
#endif

#ifdef DEBUGGING
#define ASSERT(x) do { if (!(x)) bug("Assertion `%s' failed at %s:%d", #x, __FILE__, __LINE__); } while(0)
#else
#define ASSERT(x) do { } while(0)
#endif

/* Pseudorandom numbers */

u32 random_u32(void);

#endif
