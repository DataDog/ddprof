#ifndef _H_DICTIONARY
#define _H_DICTIONARY

#include <sys/mman.h>
#include <sys/types.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef D_ALLOC_DBG
#include <errno.h>
#include <stdio.h>

#  define P_ADBG(x) if(MAP_FAILED == (x)) printf("%d: %s\n", __LINE__, strerror(errno))
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

/*
 * A couple of notes:
 * StringTable's arena is a power-of-two.  It tries to use Linux features to
 * resize-in-place and copies if that fails.  An additional failover would be to
 * implement incremental rehashing, but punting on that for now.
 * Similarly, the nodes are power-of-two.
 *
 * THE MOST IMPORTANT NOTE OF ALL:
 * All of the strings interned by this library into the string arena are
 * prepended by a FOUR BYTE LENGTH.  Yes, you are reading this correctly.  This
 * library inserts garbage into the string table, presuming that nobody is
 * going to want to serialize the whole thing in one go.
 */

typedef struct StringTableNode {
  unsigned char* string;        // Pointer into the arena
  ssize_t idx;                  // Index into the table
  struct StringTableNode* next; // If this is linked, the next guy
} StringTableNode;

// ONLY FOR INTERNAL USE.  ONLY.
#define STR_LEN_PTR(x) ((uint32_t*)&x[-4])
#define STR_LEN(x) (*STR_LEN_PTR(x))

typedef struct StringTable {
  // Elements governing the string arena
  unsigned char* arena;      // The place where the strings live
  uint32_t arena_reserved;   // How many BYTES are reserved
  uint32_t arena_size;       // How many bytes of the arena are used

  // Elements governing the node arena
  StringTableNode*  nodes;   // Arena
  StringTableNode** entry;   // Indirection for hashing
  uint32_t nodes_reserved;   // How many ELEMENTS are reserved
  uint32_t nodes_size;

  // For convenience to prevent having to walk the table again later
  unsigned char** table;
  uint32_t table_reserved;  // ELEMENTS
  uint32_t table_size;

  // Governs the semantics of this string table
  // TODO 32 or 64-bit hashing?  Common wisdom is that dictionaries benefit
  //      from a 32-bit hash for reasons of cache-efficiency.
  uint32_t (*hash_fun)(unsigned char* key, size_t len);
  uint8_t logging      : 1, // disabled by default
          hash_type    : 2, // 0-djb2, 1-wyhash, 2-???; CURRENTLY UNUSED, good intentions etc
          __reserved_0 : 5; // Right.
} StringTable;

#define ST_ARENA_SIZE 16384 // Starting number of bytes for variable-sized arenas
#define ST_ARENA_NELEM 4096 // Starting number of elements for fixed-size arenas

// Internal functions
static void _StringTable_arena_init(StringTable*);
static void _StringTable_arena_resize(StringTable*);
static void _StringTable_arena_free(StringTable*);
static unsigned char* _StringTable_arena_add(StringTable*, unsigned char*, size_t);
static void _StringTable_nodes_init(StringTable*);
static void _StringTable_nodes_resize(StringTable*);
static void _StringTable_nodes_free(StringTable*);
static StringTableNode* _StringTable_nodes_add(StringTable*, unsigned char*, ssize_t);
static void _StringTable_table_init(StringTable*);
static void _StringTable_table_resize(StringTable*);
static void _StringTable_table_free(StringTable*);
static ssize_t _StringTable_table_add(StringTable*, unsigned char*);

// Public API
StringTable* stringtable_init();
void stringtable_free(StringTable*);
ssize_t stringtable_lookup(StringTable*, unsigned char*, size_t);
unsigned char* stringtable_get(StringTable*, ssize_t);
ssize_t stringtable_add(StringTable*, unsigned char*, size_t);
ssize_t stringtable_add_cstr(StringTable*, char*);

// -------- Implementation
// ---- StringTable Arena
static void _StringTable_arena_init(StringTable* st) {
// TODO This can be changed to allow multiple threads or processes to coordinate
//      on the same string table.  The outline is to mmap() to a file or tmp,
//      then during initialization make sure each StringTable has a unique name
//      to refer to that file.  Every region needs to live in its own file
//      (3?), so need to juggle O_CREAT with sequenced file existence checks.
//      Obviously will be MAP_SHARED
  st->arena = mmap(NULL, ST_ARENA_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  st->arena_reserved = ST_ARENA_SIZE;
  st->arena_size = 0;

P_ADBG(st->arena);
}

static void _StringTable_arena_resize(StringTable* st) {
  unsigned char* buf = mremap(st->arena, st->arena_reserved, 2*st->arena_reserved, MREMAP_MAYMOVE);
P_ADBG(buf);
  if(MAP_FAILED == buf) {} // TODO

  st->arena_reserved *= 2;
  st->arena = buf;
}

static void _StringTable_arena_free(StringTable* st) {
  if(st->arena) munmap(st->arena, st->arena_reserved);
  st->arena = NULL;
}

static unsigned char* _StringTable_arena_add(StringTable* st, unsigned char* str, size_t len) {
  // TODO, the user has no idea if their string was resized.
  // TODO, harmonize the length checks
  // TODO, detect resize errors
  // NB, the offset arena + size is always the first unused byte of the arena
  if(len > ST_ARENA_SIZE)                      len = ST_ARENA_SIZE - 1 - sizeof(uint32_t); // I hope not!
  if(len > (2ull<<32) - 1 - sizeof(uint32_t))  len = (2ull<<32)    - 1 - sizeof(uint32_t);
  while(st->arena_reserved - st->arena_size < sizeof(uint32_t)+ 1 + len) _StringTable_arena_resize(st);

  // Now we can add it
  unsigned char* ret;
  memcpy(st->arena, (uint32_t*)&len, sizeof(uint32_t)); st->arena += sizeof(uint32_t);
  ret = memcpy(st->arena, str, len);                    st->arena += len;
  memset(st->arena, 0, 1);                              st->arena += 1;
  return ret;
}

// ---- StringTable Nodes
static void _StringTable_nodes_init(StringTable* st) {
  st->nodes = mmap(NULL, ST_ARENA_NELEM*sizeof(StringTableNode), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  st->entry = mmap(NULL, ST_ARENA_NELEM*sizeof(StringTableNode*), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  st->nodes_reserved = ST_ARENA_NELEM;
  st->nodes_size = 0;
P_ADBG(st->nodes);
P_ADBG(st->entry);

  // Especially important to clear the entries
  memset(st->entry, 0, st->nodes_reserved*sizeof(StringTableNode*));
}

static void _StringTable_nodes_resize(StringTable* st) {
  // NOTE, this actually oversizes the entries, since that's the array of leading nodes for each hash, and a hash may have collisions.
  //       the way to handle this is straightforward, but omitted for now.
  StringTableNode* buf_nodes  = mremap(st->nodes, st->nodes_reserved*sizeof(StringTableNode), 2*st->nodes_reserved*sizeof(StringTableNode), MREMAP_MAYMOVE);
  StringTableNode** buf_entry = mmap(NULL, 2*st->nodes_reserved*sizeof(StringTableNode*), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);  // Have to rehash!
P_ADBG(buf_nodes);
P_ADBG(buf_entry);
  // TODO check errors
  // TODO if we are to support incremental rehashing, this is where we set the metadata

  // Rehash
  // TODO in addition to being oversized, this is actually a dubious strategy.  We're trying to make sure that the
  //      number of slots in the hash table is roughly equivalent to the number of entries because it's the simplest
  //      way of ensuring that conflicts remain low.  The thing is, there's a tradeoff here between rehashes and
  //      marginally higher lookups (remember that lookups check string length, so the *expensive* case is when
  //      multiple strings conflict AND they have equal lengths).
  memset(buf_entry, 0, 2*st->nodes_reserved*sizeof(StringTableNode*));
  for(ssize_t i = 0; i<st->nodes_size; i++) {
    if(!st->entry[i]) continue;
    ssize_t i2 = st->hash_fun(st->entry[i]->string, STR_LEN(st->entry[i]->string)) & (2*st->nodes_reserved - 1);
    buf_entry[i2] = st->entry[i];
  }
  munmap(st->entry, st->nodes_reserved*sizeof(StringTableNode*));

  // Update
  st->nodes_reserved *= 2;
  st->nodes = buf_nodes;
  st->entry = buf_entry;
}

static void _StringTable_nodes_free(StringTable* st) {
  if(st->nodes) munmap(st->nodes, st->nodes_reserved*sizeof(StringTableNode));
  if(st->entry) munmap(st->entry, st->nodes_reserved*sizeof(StringTableNode*));
  st->nodes_reserved = 0;
  st->nodes_size = 0;
  st->nodes = NULL;
  st->entry = NULL;
}

static StringTableNode* _StringTable_nodes_add(StringTable* st, unsigned char* arena_str, ssize_t idx) {
  while(st->nodes_reserved <= st->nodes_size) _StringTable_nodes_resize(st);

  size_t i = st->nodes_size++;
  StringTableNode* node = &st->nodes[i];

  node->string = arena_str;
  node->idx = idx;
  node->next = NULL;
  return node;
}

// ---- StringTable Table
static void _StringTable_table_init(StringTable* st) {
  st->table = mmap(NULL, ST_ARENA_NELEM*sizeof(char*), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  st->table_reserved = ST_ARENA_NELEM;
  st->table_size = 0;
P_ADBG(st->table);
P_ADBG(st->nodes);
P_ADBG(st->entry);
}

static void _StringTable_table_resize(StringTable* st) {
  unsigned char** buf = mremap(st->table, st->table_reserved*sizeof(char*), 2*st->table_reserved*sizeof(char*), MREMAP_MAYMOVE);
P_ADBG(buf);
  // TODO errors

  st->table = buf;
  st->table_reserved *= 2;
}

static void _StringTable_table_free(StringTable* st) {
  if(st->table) munmap(st->table, st->table_reserved*sizeof(char*));
  st->table = NULL;
}

static ssize_t _StringTable_table_add(StringTable* st, unsigned char* arena_str) {
  while(st->table_reserved <= st->table_size) _StringTable_table_resize(st);

  ssize_t idx = st->table_size++;
  st->table[idx] = arena_str;
  return idx;
}

// ---- StringTable
StringTable* stringtable_init() {
  StringTable* ret = calloc(1, sizeof(StringTable));
  if(ret) {
    _StringTable_arena_init(ret);
    _StringTable_nodes_init(ret);
    _StringTable_table_init(ret);

    // Be explicit about options
    ret->logging = 1;
    ret->hash_fun = wyhash_hash;
  }
  return ret; // NB NULL if borked
}

void stringtable_free(StringTable* st) {
  _StringTable_arena_free(st);
  _StringTable_nodes_free(st);
  _StringTable_table_free(st);

  free(st); st = NULL;
}

// Gives -1 if string is not interned, the index if it is
ssize_t stringtable_lookup(StringTable* st, unsigned char* str, size_t len) {
  ssize_t h = st->hash_fun(str, len) & (st->nodes_reserved - 1);
  StringTableNode* node = st->entry[h];
  if(!node) return -1; // Not found!

  while(len != STR_LEN(node->string) && memcmp(str, node->string, len)) {
    if(!node->next)
      return -1;
    node = node->next;
  }
  return node->idx;
}

unsigned char* stringtable_get(StringTable* st, ssize_t idx) {
  if(idx < 0 || idx > st->table_size) return NULL;
  return st->table[idx];
}

ssize_t stringtable_add(StringTable* st, unsigned char* str, size_t len) {
  ssize_t idx = stringtable_lookup(st, str, len);
  if(-1 == idx) {
    // TODO, the mask can overrun.  :)
    unsigned char* arena_str = _StringTable_arena_add(st, str, len); // Add to the arena

    // Register in the string table
    idx = _StringTable_table_add(st, arena_str);

    // Add to the nodes
    StringTableNode* node = _StringTable_nodes_add(st, arena_str, idx);

    // Compute the hash and update the entries
    uint32_t h = st->hash_fun(str, len) & (st->nodes_reserved - 1);
    if(!st->entry[h]) {
      st->entry[h] = node;
    } else {
      StringTableNode* entry = st->entry[h];
      while(entry->next) entry = entry->next;
      entry->next = node;
    }

    // If we were compiled with logging support AND logging is enabled, then
    // write to a newline-delimited file for later analysis.
#ifdef D_LOGGING_ENABLE
#include <unistd.h>
    static char tmp_name[32] = "/tmp/stringtableXXXXXX" ".log";
    static int tmp_fd = -111;
    if(st->logging) {
      if(tmp_fd == -111) {
        tmp_fd = mkstemps(tmp_name, strlen(".log"));
        printf("Saving log to %s\n", tmp_name);
      }
      write(tmp_fd, arena_str, STR_LEN(arena_str));
      write(tmp_fd, "\n", 1);
    }
#endif
  }

  return idx;
}

ssize_t stringtable_lookup_cstr(StringTable* st, char* str) {
  return stringtable_lookup(st, (unsigned char*)str, strlen(str));
}

ssize_t stringtable_add_cstr(StringTable* st, char* str) {
  return stringtable_add(st, (unsigned char*)str, strlen(str));
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
