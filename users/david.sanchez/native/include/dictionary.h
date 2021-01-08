#ifndef _H_DICTIONARY
#define _H_DICTIONARY

#include <sys/mman.h>
#include <sys/types.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef D_ALLOC_DBG
#  include <errno.h>
#  include <stdio.h>
#  define P_ADBG(x) if(MAP_FAILED == (x)) printf("%d: %s\n", __LINE__, strerror(errno)); else printf("%d: %s\n", __LINE__, #x)
#else
#  define P_ADBG(x) do {}while(0)
#endif

// ---- Internal hash functions
// NB, this is not a sophisticated hashing strategy.
uint32_t djb2_hash(unsigned char* str, size_t len) {
  uint32_t ret = 5381;

  for(;len;len--)
    ret = ((ret << 5) + ret) + *str++;
  return ret;
}

static inline unsigned wyhash32(const void*, uint64_t, unsigned);
uint32_t wyhash_hash(unsigned char* str, size_t len) {
  static unsigned seed = 3913693727; // random large 32-bit prime
  return wyhash32((const void*)str, len, seed);
}

typedef struct DictNode {
  int64_t key;           // Actually an offset from top of arena
  int64_t val;           // Ditto
  struct DictNode* next; // If this is linked, the next guy
} DictNode;

// ONLY FOR INTERNAL USE.  ONLY.
#define STR_LEN_PTR(x) ((uint32_t*)&(x)[-4])
#define STR_LEN(x) (*STR_LEN_PTR(x))

typedef struct Dict {
  // Elements governing the string arena
  unsigned char* arena;      // The place where the strings live
  uint32_t arena_reserved;   // How many BYTES are reserved
  uint32_t arena_size;       // How many bytes of the arena are used

  // Elements governing the node arena
  DictNode*  nodes;        // Arena
  DictNode** entry;        // Indirection for hashing
  uint32_t nodes_reserved; // How many ELEMENTS are reserved
  uint32_t nodes_size;     // How many elements are used

  uint32_t (*hash_fun)(unsigned char* key, size_t len);
  uint8_t logging      : 1, // UNIMPLEMENTED
          hash_type    : 2, // 0-djb2, 1-wyhash, 2-???; CURRENTLY UNUSED, good intentions etc
          __reserved_0 : 5; // Right.
} Dict;

#define DICT_ARENA_SIZE 16384 // Starting number of bytes for variable-sized arenas
#define DICT_ARENA_NELEM 4096 // Starting number of elements for fixed-size arenas

// Internal functions
static char DictNode_add(DictNode**, DictNode*, uint32_t);
static void _Dict_arena_init(Dict*);
static void _Dict_arena_resize(Dict*);
static void _Dict_arena_free(Dict*);
static int64_t _Dict_arena_add(Dict*, unsigned char*, size_t);
static void _Dict_nodes_init(Dict*);
static void _Dict_nodes_resize(Dict*);
static void _Dict_nodes_free(Dict*);
static DictNode* _Dict_nodes_add(Dict*, int64_t, int64_t);

// Public API
Dict* dict_init();
void dict_free(Dict*);
unsigned char* dict_get(Dict*, unsigned char*, size_t);
unsigned char* dict_get_1(Dict*, char*);
char dict_add(Dict*, unsigned char*, size_t, unsigned char*, size_t);
char dict_add_01(Dict*, unsigned char*, size_t, char*);
char dict_add_10(Dict*, char*, unsigned char*, size_t);
char dict_add_11(Dict*, char*, char*);

// -------- Implementation
// ---- Dict Arena
static void _Dict_arena_init(Dict* dict) {
  dict->arena = mmap(NULL, DICT_ARENA_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  dict->arena_reserved = DICT_ARENA_SIZE;
  dict->arena_size = 0;

P_ADBG(dict->arena);
}

static void _Dict_arena_resize(Dict* dict) {
  unsigned char* arena_resize_buf = mremap(dict->arena, dict->arena_reserved, 2*dict->arena_reserved, MREMAP_MAYMOVE);
P_ADBG(arena_resize_buf);
  if(MAP_FAILED == arena_resize_buf) {} // TODO

  dict->arena_reserved *= 2;
  dict->arena = arena_resize_buf;
}

static void _Dict_arena_free(Dict* dict) {
  if(dict->arena) munmap(dict->arena, dict->arena_reserved);
  dict->arena = NULL;
}

static int64_t _Dict_arena_add(Dict* dict, unsigned char* str, size_t len) {
  // TODO, the user has no idea if their string was resized.
  // TODO, harmonize the length checks
  // TODO, detect resize errors
  // NB, the offset arena + size is always the first unused byte of the arena
  if(len > DICT_ARENA_SIZE)
    len = DICT_ARENA_SIZE - 1 - sizeof(uint32_t); // I hope not!
  if(len > (2ull<<32) - 1 - sizeof(uint32_t))
    len = (2ull<<32)- 1 - sizeof(uint32_t);
  while(dict->arena_reserved < dict->arena_size + sizeof(uint32_t) + 1 + len)
    _Dict_arena_resize(dict);

  // Now we can add it
  unsigned char* ret;
  size_t i = dict->arena_size;
  memcpy(&dict->arena[i], (uint32_t*)&len, sizeof(uint32_t)); i += sizeof(uint32_t);
  ret = memcpy(&dict->arena[i], str, len);                    i += len;
  memset(&dict->arena[i], 0, 1);                              i += 1;
  dict->arena_size += sizeof(uint32_t) + len + 1;
  return ret - dict->arena;  // Return an offset!
}

// ---- Dict Nodes
static void _Dict_nodes_init(Dict* dict) {
  dict->nodes = mmap(NULL, DICT_ARENA_NELEM*sizeof(DictNode), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  dict->entry = mmap(NULL, DICT_ARENA_NELEM*sizeof(DictNode*), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  dict->nodes_reserved = DICT_ARENA_NELEM;
  dict->nodes_size = 0;
P_ADBG(dict->nodes);
P_ADBG(dict->entry);

  // Especially important to clear the entries
  memset(dict->entry, 0, dict->nodes_reserved*sizeof(DictNode*));
}

static void _Dict_nodes_resize(Dict* dict) {
  // NOTE, this actually oversizes the entries, since that's the array of leading nodes for each hash, and a hash may have collisions.
  //       the way to handle this is straightforward, but omitted for now.
  DictNode* buf_nodes  = mremap(dict->nodes, dict->nodes_reserved*sizeof(DictNode), 2*dict->nodes_reserved*sizeof(DictNode), MREMAP_MAYMOVE);
  DictNode** buf_entry = mmap(NULL, 2*dict->nodes_reserved*sizeof(DictNode*), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);  // Have to rehash!
P_ADBG(buf_nodes);
P_ADBG(buf_entry);
  // TODO check errors
  // TODO if we are to support incremental rehashing, this is where we set the metadata

  // Rehash
  // The other way to do this is to create a flag specifying whether a given
  // arena member is populated, then just start from the top by hashing.
  // This could potentially terminate in far fewer steps, but with one
  // additional hash per step.  This is a desirable strategy if the resident
  // fraction of the hash table exceeds the time multiplier of the additional
  // hash.  We could implement an online fraction update (if adding a top-level
  // entry, not a child of an existing entry, increment) and toggle between the
  // strategies independently of the underlying hash function.  We just do it
  // the easy way here.
  // This depends on having an arena strictly for keys, which we do not.
  memset(buf_entry, 0, 2*dict->nodes_reserved*sizeof(DictNode*));
  uint32_t mask = 2*dict->nodes_reserved - 1;
  for(ssize_t i = 0; i<dict->nodes_reserved; i++) {
    if(!dict->entry[i]) continue; // Nothing in this position, keep going

    // Generate hash for resized table
    unsigned char* this_key = dict->arena + dict->entry[i]->key;
    ssize_t i2 = dict->hash_fun(this_key, STR_LEN(this_key)) & mask;

    // Insert into new entries field.  We can keep the old arena, since the node
    // just hangs onto offsets
    DictNode_add(buf_entry, dict->entry[i], i2);
  }
  munmap(dict->entry, dict->nodes_reserved*sizeof(DictNode*));

  // Update
  dict->nodes_reserved *= 2;
  dict->nodes = buf_nodes;
  dict->entry = buf_entry;
}

static void _Dict_nodes_free(Dict* dict) {
  if(dict->nodes) munmap(dict->nodes, dict->nodes_reserved*sizeof(DictNode));
  if(dict->entry) munmap(dict->entry, dict->nodes_reserved*sizeof(DictNode*));
  dict->nodes_reserved = 0;
  dict->nodes_size = 0;
  dict->nodes = NULL;
  dict->entry = NULL;
}

static DictNode* _Dict_nodes_add(Dict* dict, int64_t arena_key, int64_t arena_val) {
  while(dict->nodes_reserved <= dict->nodes_size) _Dict_nodes_resize(dict);

  size_t i = dict->nodes_size++;
  DictNode* node = &dict->nodes[i];

  node->key = arena_key;
  node->val = arena_val;
  node->next = NULL;
  return node;
}


// ---- DictNode
static char DictNode_add(DictNode** entry, DictNode* node, uint32_t h) {
  // assume h fits
  if(!entry || !node) return -1;

  if(!entry[h]) {
    entry[h] = node;
  } else {
    DictNode* parent = entry[h];
    while(parent->next) parent = parent->next;
    parent->next = node;
  }
  return 0;
}

// ---- Dict
Dict* dict_init() {
  Dict* ret = calloc(1, sizeof(Dict));
  if(ret) {
    _Dict_arena_init(ret);
    _Dict_nodes_init(ret);

    // Be explicit about options
    ret->logging = 0; // UNIMPLEMENTED
    ret->hash_fun = wyhash_hash;
  }
  return ret; // NB NULL if borked
}

void dict_free(Dict* dict) {
  // Caller must free dict, since it might be allocated on the stack
  _Dict_arena_free(dict);
  _Dict_nodes_free(dict);
}

// Gives NULL if key is not populated, ptr to arena val if it is
unsigned char* dict_get(Dict* dict, unsigned char* key, size_t len) {
  ssize_t h = dict->hash_fun(key, len) & (dict->nodes_reserved - 1);
  DictNode* node = dict->entry[h];
  if(!node) return NULL; // Not found!

  while(len != STR_LEN(dict->arena + node->key) &&
        memcmp(key, dict->arena + node->key, len)) {
    if(!node->next)
      return NULL;
    node = node->next;
  }
  return node->val + dict->arena;
}

unsigned char* dict_get_1(Dict* dict, char* key) {
  return dict_get(dict, (unsigned char*)key, strlen(key));
}

char dict_add(Dict* dict, unsigned char* key, size_t key_sz, unsigned char* val, size_t val_sz) {
  unsigned char* found = dict_get(dict, key, key_sz);
  if(!found) {
    // TODO, the mask can overrun.  :)
    int64_t arena_key = _Dict_arena_add(dict, key, key_sz);
    if(!arena_key) return -1;
    int64_t arena_val = _Dict_arena_add(dict, val, val_sz);
    if(!arena_val) return -1;

    // Add to the nodes
    DictNode* node = _Dict_nodes_add(dict, arena_key, arena_val);

    // Compute the hash and update the entries
    uint32_t h = dict->hash_fun(key, key_sz) & (dict->nodes_reserved - 1);
    if(DictNode_add(dict->entry, node, h)) return -1; // TODO better interface
  }

  return 0;
}

char dict_add_01(Dict* dict, unsigned char* key, size_t key_sz, char* val) {
  return dict_add(dict, key, key_sz, (unsigned char*)val, strlen(val));
}

char dict_add_10(Dict* dict, char* key, unsigned char* val, size_t val_sz) {
  return dict_add(dict, (unsigned char*)key, strlen(key), val, val_sz);
}

char dict_add_11(Dict* dict, char* key, char* val) {
  return dict_add(dict, (unsigned char*)key, strlen(key), (unsigned char*)val, strlen(val));
}

// Let's make the world a safer place by disabling these
#undef STR_LEN
#undef STR_LEN_PTR


/******************************************************************************\
|*                      Inlined wyhash32 Implementation                       *|
\******************************************************************************/
// The remainder of this file is attributed as noted.  Provided inline merely to
// make this library importable as a single-file include.

// Author: Wang Yi <godspeed_china@yeah.net>
#include <stdint.h>
#include <string.h>
#ifndef WYHASH32_BIG_ENDIAN
static inline unsigned _wyr32(const uint8_t *p) { unsigned v; memcpy(&v, p, 4); return v;}
#elif defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__clang__)
static inline unsigned _wyr32(const uint8_t *p) { unsigned v; memcpy(&v, p, 4); return __builtin_bswap32(v);}
#elif defined(_MSC_VER)
static inline unsigned _wyr32(const uint8_t *p) { unsigned v; memcpy(&v, p, 4); return _byteswap_ulong(v);}
#endif
static inline unsigned _wyr24(const uint8_t *p, unsigned k) { return (((unsigned)p[0])<<16)|(((unsigned)p[k>>1])<<8)|p[k-1];}
static inline void _wymix32(unsigned  *A,  unsigned  *B){
  uint64_t  c=*A^0x53c5ca59u;  c*=*B^0x74743c1bu;
  *A=(unsigned)c;
  *B=(unsigned)(c>>32);
}
static inline unsigned wyhash32(const void *key, uint64_t len, unsigned seed) {
  const uint8_t *p=(const uint8_t *)key; uint64_t i=len;
  unsigned see1=(unsigned)len; seed^=(unsigned)(len>>32); _wymix32(&seed, &see1);
  for(;i>8;i-=8,p+=8){  seed^=_wyr32(p); see1^=_wyr32(p+4); _wymix32(&seed, &see1); }
  if(i>=4){ seed^=_wyr32(p); see1^=_wyr32(p+i-4); } else if (i) seed^=_wyr24(p,i);
  _wymix32(&seed, &see1); _wymix32(&seed, &see1); return seed^see1;
}
static inline uint64_t wyrand(uint64_t *seed){
  *seed+=0xa0761d6478bd642full;
  uint64_t  see1=*seed^0xe7037ed1a0b428dbull;
  see1*=(see1>>32)|(see1<<32);
  return  (*seed*((*seed>>32)|(*seed<<32)))^((see1>>32)|(see1<<32));
}
static inline unsigned wy32x32(unsigned a,  unsigned  b) { _wymix32(&a,&b); _wymix32(&a,&b); return a^b;  }
static inline float wy2u01(unsigned r) { const float _wynorm=1.0f/(1ull<<23); return (r>>9)*_wynorm;}
static inline float wy2gau(unsigned r) { const float _wynorm=1.0f/(1ull<<9); return ((r&0x3ff)+((r>>10)&0x3ff)+((r>>20)&0x3ff))*_wynorm-3.0f;}

#endif
