/*
 *	Unaligned Data Accesses -- Generic Version
 *
 *	(c) 1998 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_UNALIGNED_H_
#define _BIRD_UNALIGNED_H_

#if CPU_NEEDS_ALIGN_WORD != 1 || CPU_NEEDS_ALIGN_LONG != 1
#include <string.h>
#endif

#if CPU_NEEDS_ALIGN_WORD == 1
#define unaligned_u16(p) (*((u16 *)(p)))
#else
static inline u16
unaligned_u16(void *p)
{
  u16 x;

  memcpy(&x, p, sizeof(x));
  return x;
}
#endif

#if CPU_NEEDS_ALIGN_LONG == 1
#define unaligned_u32(p) (*((u32 *)(p)))
#else
static inline u32
unaligned_u32(void *p)
{
  u32 x;

  memcpy(&x, p, sizeof(x));
  return x;
}
#endif

#endif
