/*
 *	This file contains all parameters dependent on the
 *	operating system and build-time configuration.
 */

#ifndef _BIRD_CONFIG_H_
#define _BIRD_CONFIG_H_

/* BIRD version */
#define BIRD_VERSION "0.0.0"

/* Include parameters determined by configure script */
#include "sysdep/autoconf.h"

/* Include OS configuration file as chosen in autoconf.h */
#include SYSCONF_INCLUDE

#ifndef MACROS_ONLY

#include "sysdep/paths.h"

/* Types */
typedef signed INTEGER_8 s8;
typedef unsigned INTEGER_8 u8;
typedef INTEGER_16 s16;
typedef unsigned INTEGER_16 u16;
typedef INTEGER_32 s32;
typedef unsigned INTEGER_32 u32;
typedef u8 byte;
typedef u16 word;

#endif

/* Path to configuration file */
#ifdef DEBUGGING
#define PATH_CONFIG "bird.conf"
#define PATH_CONTROL_SOCKET "bird.ctl"
#else
#define PATH_CONFIG PATH_CONFIG_DIR "/bird.conf"
#define PATH_CONTROL_SOCKET PATH_CONTROL_SOCKET_DIR "/bird.ctl"
#endif

#endif
