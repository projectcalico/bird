/*
 *	This is a manually generated BIRD configuration file.
 *	It will be replaced by something better when we come
 *	with an automated configuration mechanism. [mj]
 */

#ifndef _BIRD_CONFIG_H_
#define _BIRD_CONFIG_H_

/* System-dependent configuration */

#include <sysdep/cf/linux-20.h>

/* Include debugging code */

#define DEBUG

/* Types */

typedef signed char s8;
typedef unsigned char u8;
typedef short int s16;
typedef unsigned short int u16;
typedef int s32;
typedef unsigned int u32;

typedef u8 byte;
typedef u16 word;

/* Endianity */

#define CPU_LITTLE_ENDIAN

/* Required alignment for multi-byte accesses */

#define CPU_NEEDS_ALIGN_WORD 1
#define CPU_NEEDS_ALIGN_LONG 1

/* Usual alignment for structures */

#define CPU_STRUCT_ALIGN 4

/* Protocol options */

#define CONFIG_STATIC
#define CONFIG_RIP
#define CONFIG_BGP
#define CONFIG_OSPF

#endif
