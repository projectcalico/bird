/*
 *	This file contains all parameters dependent on the
 *	operating system and build-time configuration.
 */

#ifndef _BIRD_CONFIG_H_
#define _BIRD_CONFIG_H_

/* Include parameters determined by configure script */
#include "sysdep/autoconf.h"

/* Include OS configuration file as chosen in autoconf.h */
#include SYSCONF_INCLUDE

/* Types */
typedef signed INTEGER_8 s8;
typedef unsigned INTEGER_8 u8;
typedef INTEGER_16 s16;
typedef unsigned INTEGER_16 u16;
typedef INTEGER_32 s32;
typedef unsigned INTEGER_32 u32;
typedef u8 byte;
typedef u16 word;

/*
 * Required alignment for multi-byte accesses. We currently don't
 * test these values in configure script, because several CPU's
 * have unaligned accesses emulated by OS and they are slower
 * than a call to memcpy() which is, in fact, often compiled
 * as load/store by GCC on machines which don't require alignment.
 */

#define CPU_NEEDS_ALIGN_WORD 2
#define CPU_NEEDS_ALIGN_LONG 4

/* Path to configuration file */
#define PATH_CONFIG PATH_CONFIG_DIR "/bird.conf"

#endif
