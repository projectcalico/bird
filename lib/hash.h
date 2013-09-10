

#define HASH(type)		struct { type **data; uint used, size; }
#define HASH_TYPE(v)		typeof(** (v).data)
#define HASH_SIZE(v)		((v).size * sizeof(* (v).data))

#define HASH_INIT(v,pool,isize)						\
  ({									\
    (v).used = 0;							\
    (v).size = (isize);							\
    (v).data = mb_allocz(pool, HASH_SIZE(v));				\
  })

#define HASH_FIND(v,id,key)						\
  ({									\
    HASH_TYPE(v) *_n = (v).data[id##_FN(key, (v).size)];		\
    while (_n && !id##_EQ(_n, key))					\
      _n = _n->id##_NEXT;						\
    _n;									\
  })

#define HASH_INSERT(v,id,key,node)					\
  ({									\
    HASH_TYPE(v) **_nn = (v).data + id##_FN(key, (v).size);		\
    node->id##_NEXT = *_nn;						\
    *_nn = node;							\
  })

#define HASH_DELETE(v,id,key)						\
  ({									\
    HASH_TYPE(v) **_nn = (v).data + id##_FN(key, (v).size);		\
    while ((*_nn) && !id##_EQ(*_nn, key))				\
      _nn = &((*_nn)->id##_NEXT);					\
									\
    HASH_TYPE(v) *_n = *_nn;						\
    if (_n)								\
      *_nn = _n->id##_NEXT;						\
    _n;									\
  })

#define HASH_REMOVE(v,id,node)						\
  ({									\
    HASH_TYPE(v) **_nn = (v).data + id##_FN(key, (v).size);		\
    while ((*_nn) && (*_nn != (node)))					\
      _nn = &((*_nn)->id##_NEXT);					\
									\
    HASH_TYPE(v) *_n = *_nn;						\
    if (_n)								\
      *_nn = _n->id##_NEXT;						\
    _n;									\
  })



#define HASH_WALK(v,next,n)						\
  do {									\
    HASH_TYPE(v) *n;							\
    uint _i;								\
    for (_i = 0; _i < ((v).size); _i++)					\
      for (n = (v).data[_i]; n; n = n->next)

#define HASH_WALK_END } while (0)


#define HASH_WALK_DELSAFE(v,next,n)					\
  do {									\
    HASH_TYPE(v) *n, *_next;						\
    uint _i;								\
    for (_i = 0; _i < ((v).size); _i++)					\
      for (n = (v).data[_i]; n && (_next = n->next, 1); n = _next)

#define HASH_WALK_DELSAFE_END } while (0)

/*
define HASH_REHASH(s)							\
  ({									\
    type *_n;								\
    uint _i;								\
    for (_i = 0; _i < (size_f); _i++)					\
      for (_n = (hash)[_i]; _n != NULL; _n = 

*/
  
