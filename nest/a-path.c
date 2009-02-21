/*
 *	BIRD -- Path Operations
 *
 *	(c) 2000 Martin Mares <mj@ucw.cz>
 *	(c) 2000 Pavel Machek <pavel@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "nest/bird.h"
#include "nest/route.h"
#include "nest/attrs.h"
#include "lib/resource.h"
#include "lib/unaligned.h"
#include "lib/string.h"


/* Global AS4 support, shared by all BGP instances.
 * This specifies whether BA_AS_PATH attributes contain 2 or 4 B per ASN
 */

int bgp_as4_support = 1;

static void
put_as(byte *data, u32 as)
{
  if (bgp_as4_support)
    put_u32(data, as);
  else if (as <= 0xFFFF)
    put_u16(data, as);
  else
    bug("put_as: Try to put 32bit AS to 16bit AS Path");
}

static inline u32
get_as(byte *data)
{
  return bgp_as4_support ? get_u32(data) : get_u16(data);
}

struct adata *
as_path_prepend(struct linpool *pool, struct adata *olda, u32 as)
{
  int bs = bgp_as4_support ? 4 : 2;
  struct adata *newa;

  if (olda->length && olda->data[0] == AS_PATH_SEQUENCE && olda->data[1] < 255)
    /* Starting with sequence => just prepend the AS number */
    {
      int nl = olda->length + bs;
      newa = lp_alloc(pool, sizeof(struct adata) + nl);
      newa->length = nl;
      newa->data[0] = AS_PATH_SEQUENCE;
      newa->data[1] = olda->data[1] + 1;
      memcpy(newa->data + bs + 2, olda->data + 2, olda->length - 2);
    }
  else /* Create new path segment */
    {
      int nl = olda->length + bs + 2;
      newa = lp_alloc(pool, sizeof(struct adata) + nl);
      newa->length = nl;
      newa->data[0] = AS_PATH_SEQUENCE;
      newa->data[1] = 1;
      memcpy(newa->data + bs + 2, olda->data, olda->length);
    }
  put_as(newa->data + 2, as);
  return newa;
}

int
as_path_convert_to_old(struct adata *path, byte *dst, int *new_used)
{
  byte *src = path->data;
  byte *src_end = src + path->length;
  byte *dst_start = dst;
  u32 as;
  int i, n;
  *new_used = 0;

  while (src < src_end)
    {
      n = src[1];
      *dst++ = *src++;
      *dst++ = *src++;

      for(i=0; i<n; i++)
	{
	  as = get_u32(src);
	  if (as > 0xFFFF) 
	    {
	      as = AS_TRANS;
	      *new_used = 1;
	    }
	  put_u16(dst, as);
	  src += 4;
	  dst += 2;
	}
    }

  return dst - dst_start;
}

int
as_path_convert_to_new(struct adata *path, byte *dst, int req_as)
{
  byte *src = path->data;
  byte *src_end = src + path->length;
  byte *dst_start = dst;
  u32 as;
  int i, t, n;


  while ((src < src_end) && (req_as > 0))
    {
      t = *src++;
      n = *src++;

      if (t == AS_PATH_SEQUENCE)
	{
	  if (n > req_as)
	    n = req_as;

	  req_as -= n;
	}
      else // t == AS_PATH_SET
	req_as--;

      *dst++ = t;
      *dst++ = n;

      for(i=0; i<n; i++)
	{
	  as = get_u16(src);
	  put_u32(dst, as);
	  src += 2;
	  dst += 4;
	}
    }

  return dst - dst_start;
}

void
as_path_format(struct adata *path, byte *buf, unsigned int size)
{
  int bs = bgp_as4_support ? 4 : 2;
  byte *p = path->data;
  byte *e = p + path->length;
  byte *end = buf + size - 16;
  int sp = 1;
  int l, isset;

  while (p < e)
    {
      if (buf > end)
	{
	  strcpy(buf, " ...");
	  return;
	}
      isset = (*p++ == AS_PATH_SET);
      l = *p++;
      if (isset)
	{
	  if (!sp)
	    *buf++ = ' ';
	  *buf++ = '{';
	  sp = 0;
	}
      while (l-- && buf <= end)
	{
	  if (!sp)
	    *buf++ = ' ';
	  buf += bsprintf(buf, "%u", get_as(p));
	  p += bs;
	  sp = 0;
	}
      if (isset)
	{
	  *buf++ = ' ';
	  *buf++ = '}';
	  sp = 0;
	}
    }
  *buf = 0;
}

int
as_path_getlen(struct adata *path)
{
  int bs = bgp_as4_support ? 4 : 2;
  int res = 0;
  u8 *p = path->data;
  u8 *q = p+path->length;
  int len;

  while (p<q)
    {
      switch (*p++)
	{
	case AS_PATH_SET:      len = *p++; res++;      p += bs * len; break;
	case AS_PATH_SEQUENCE: len = *p++; res += len; p += bs * len; break;
	default: bug("as_path_getlen: Invalid path segment");
	}
    }
  return res;
}

int
as_path_get_first(struct adata *path, u32 *orig_as)
{
  int bs = bgp_as4_support ? 4 : 2;
  int found = 0;
  u32 res = 0;
  u8 *p = path->data;
  u8 *q = p+path->length;
  int len;

  while (p<q)
    {
      switch (*p++)
	{
	case AS_PATH_SET:
	  if (len = *p++)
	    {
	      found = 1;
	      res = get_as(p);
	      p += bs * len;
	    }
	  break;
	case AS_PATH_SEQUENCE:
	  if (len = *p++)
	    {
	      found = 1;
	      res = get_as(p + bs * (len - 1));
	      p += bs * len;
	    }
	  break;
	default: bug("as_path_get_first: Invalid path segment");
	}
    }

  *orig_as = res;
  return found;
}

int
as_path_get_last(struct adata *path, u32 *last_as)
{
  u8 *p = path->data;

  if ((path->length == 0) || (p[0] != AS_PATH_SEQUENCE) || (p[1] == 0))
    return 0;
  else
    {
      *last_as = get_as(p+2);
      return 1;
    }
}

int
as_path_is_member(struct adata *path, u32 as)
{
  int bs = bgp_as4_support ? 4 : 2;
  u8 *p = path->data;
  u8 *q = p+path->length;
  int i, n;

  while (p<q)
    {
      n = p[1];
      p += 2;
      for(i=0; i<n; i++)
	{
	  if (get_as(p) == as)
	    return 1;
	  p += bs;
	}
    }
  return 0;
}



#define MASK_PLUS do { mask = mask->next; if (!mask) return next == q; \
		       asterisk = mask->any; \
                       if (asterisk) { mask = mask->next; if (!mask) { return 1; } } \
		       } while(0)

int
as_path_match(struct adata *path, struct f_path_mask *mask)
{
  int bs = bgp_as4_support ? 4 : 2;
  int i;
  int asterisk = 0;
  u8 *p = path->data;
  u8 *q = p+path->length;
  int len;
  u8 *next;
  u32 as;

  if (!mask)
    return ! path->length;

  asterisk = mask->any;
  if (asterisk)
    { mask = mask->next; if (!mask) return 1; }

  while (p<q) {
    switch (*p++) {
    case AS_PATH_SET:
      len = *p++;
      {
	u8 *p_save = p;
	next = p_save + bs * len;
      retry:
	p = p_save;
	for (i=0; i<len; i++) {
	  as = get_as(p);
	  if (asterisk && (as == mask->val)) {
	    MASK_PLUS;
	    goto retry;
	  }
	  if (!asterisk && (as == mask->val)) {
	    p = next;
	    MASK_PLUS;
	    goto okay;
	  }
	  p += bs;
	}
	if (!asterisk)
	  return 0;
      okay: ;
      }
      break;

    case AS_PATH_SEQUENCE:
      len = *p++;
      for (i=0; i<len; i++) {
	next = p + bs;
	as = get_as(p);
	if (asterisk && (as == mask->val))
	  MASK_PLUS;
	else if (!asterisk) {
	  if (as != mask->val)
	    return 0;
	  MASK_PLUS;
	}
	p += bs;
      }
      break;

    default:
      bug("as_path_match: Invalid path component");
    }
  }
  return 0;
}

