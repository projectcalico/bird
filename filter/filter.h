/*
 *	BIRD Internet Routing Daemon -- Filters
 *
 *	(c) 1999 Pavel Machek <pavel@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#ifndef _BIRD_FILT_H_
#define _BIRD_FILT_H_

#include "lib/resource.h"
#include "lib/ip.h"

struct f_inst {		/* Instruction */
  struct f_inst *next;	/* Structure is 16 bytes, anyway */
  u16 code;
  u16 aux;
  union {
    int i;
    void *p;
  } a1;
  union {
    int i;
    void *p;
  } a2;
};

#define arg1 a1.p
#define arg2 a2.p

struct prefix {
  ip_addr ip;
  int len;
#define LEN_MASK 0xff
#define LEN_PLUS  0x1000000
#define LEN_MINUS 0x2000000
#define LEN_RANGE 0x4000000
  /* If range then prefix must be in range (len >> 16 & 0xff, len >> 8 & 0xff) */
};

struct f_val {
  int type;
  union {
    int i;
    /*    ip_addr ip; Folded into prefix */	
    struct prefix px;
    char *s;
    struct f_tree *t;
  } val;
};

struct filter {
  char *name;
  struct f_inst *root;
};

void filters_postconfig(void);
struct f_inst *f_new_inst(void);
struct f_tree *f_new_tree(void);

struct f_tree *build_tree(struct f_tree *);
struct f_tree *find_tree(struct f_tree *t, struct f_val val);
int same_tree(struct f_tree *t1, struct f_tree *t2);

struct ea_list;
struct rte;

int f_run(struct filter *filter, struct rte **rte, struct ea_list **tmp_attrs, struct linpool *tmp_pool);
char *filter_name(struct filter *filter);
int filter_same(struct filter *new, struct filter *old);

int i_same(struct f_inst *f1, struct f_inst *f2);

int val_compare(struct f_val v1, struct f_val v2);
void val_print(struct f_val v);

#define F_NOP 0
#define F_NONL 1
#define F_ACCEPT 2	/* Need to preserve ordering: accepts < rejects! */
#define F_REJECT 3
#define F_ERROR 4
#define F_QUITBIRD 5

#define FILTER_ACCEPT NULL
#define FILTER_REJECT ((void *) 1)

/* Type numbers must be in 0..0xff range */
#define T_MASK 0xff

/* Internal types */
/* Do not use type of zero, that way we'll see errors easier. */
#define T_VOID 1
#define T_RETURN 2

/* User visible types, which fit in int */
#define T_INT 0x10
#define T_BOOL 0x11
#define T_PAIR 0x12

/* Put enumerational types in 0x30..0x7f range */
#define T_ENUM_LO 0x30
#define T_ENUM_HI 0x7f

#define T_ENUM_RTS 0x30

#define T_ENUM T_ENUM_LO ... T_ENUM_HI

/* Bigger ones */
#define T_IP 0x20
#define T_PREFIX 0x21
#define T_STRING 0x22

#define T_SET 0x80

struct f_tree {
  struct f_tree *left, *right;
  struct f_val from, to;
  void *data;
};

#define NEW_F_VAL struct f_val * val; val = cfg_alloc(sizeof(struct f_val));

#endif
