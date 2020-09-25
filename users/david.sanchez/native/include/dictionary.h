#ifndef  _H_dictionary
#define _H_dictionary

typedef struct DictParams {
  uint64_t (*hash_fun)(void* key);
  void* (*key_copy)(void* key);
  void  (*key_del)(void* key);
  void* (*val_copy)(void* val);
  void  (*val_del)(void* val);
  char (*equal)(void*, void*);
} DictParams;

typedef struct DictNode {
  void* key;
  union {
    void*    val;
    int64_t  i64;
    uint64_t u64;
    double   d;
  };
} DictNode;

#endif
