#ifndef _H_dso
#define _H_dso

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "string_table.h"

#define PID_MAX 32768
// For compatibility with perf events
typedef struct DsoIn {
  uint64_t addr;
  uint64_t len;
  uint64_t pgoff;
  char *filename;
} DsoIn;

typedef struct Dso {
  uint64_t start;
  uint64_t end;
  uint64_t pgoff; // offset in _bytes_ of a page; not an offset in pages
  uint32_t filename;
  struct Dso *next;
} Dso;

typedef struct PidList {
  Dso *dsos[PID_MAX];
} PidList;

typedef struct DsoCache {
  uint64_t pgoff;
  uint32_t filename;
  uint32_t sz; // optimistic?
  void *region;
} DsoCache;

PidList pids = {0};

StringTable stab_dso = {0};

void libdso_init() { stringtable_init(&stab_dso, NULL); }

void pid_free(int pid) {
  assert(pid > 0 && pid <= PID_MAX);
  Dso *dso = pids.dsos[pid];
  pids.dsos[pid] = NULL;

  while (dso) {
    Dso *next = dso->next;
    free(dso);
    dso = next;
  }
}

void pid_add(int pid, DsoIn *in) {
  assert(pid > 0 && pid <= PID_MAX);

  // If we already have a PID in this slot, then we'll assume for now that the
  // old PID is invalid and needs to be cleaned up.  It's very possible that
  // the two registrations occurred in separate PID namespaces.  For later.
  if (pids.dsos[pid])
    pid_free(pid);

  // Skip to last DSO
  Dso *dso = pids.dsos[pid];
  while (dso->next)
    dso = dso->next;

  // Allocate and populate
  dso->next = malloc(sizeof(Dso));
  dso->next->next = NULL;
  dso->next->start = in->addr;
  dso->next->end = in->addr + in->len;
  dso->next->pgoff = in->pgoff;

  ssize_t id_str = stringtable_add_cstr(&stab_dso, (const char *)in->filename);
  if (id_str >= 0)
    dso->next->filename = id_str;
}

void pid_fork(int ppid, int pid) {
  assert(pid > 0 && pid <= PID_MAX);
  assert(ppid > 0 && ppid <= PID_MAX);
  assert(pids.dsos[ppid]);

  // See disclaimer in pid_add()--shouldn't happen, but might.
  if (pids.dsos[pid])
    pid_free(pid);
  Dso *pdso = pids.dsos[ppid];
  Dso *dso = malloc(sizeof(Dso));

  // Initialize root dso for child
  pids.dsos[pid] = dso;
  memcpy(dso, pdso, sizeof(Dso));

  // Iterate and alloc + copy.
  pdso = pdso->next;
  while ((pdso = pdso->next)) {
    dso->next = malloc(sizeof(Dso));
    dso = dso->next;
    memcpy(dso, pdso, sizeof(Dso));
    dso->next = NULL;
  }
}

char *dso_path(Dso *dso) {
  assert(dso);
  return (char *)stringtable_get(&stab_dso, dso->filename);
}

Dso *dso_find(int pid, uint64_t addr) {
  assert(pid > 0 && pid <= PID_MAX);
  assert(addr > 4095); // Zero page is a thing

  // Check anyway
  if (pid <= 0 || pid > PID_MAX || !pids.dsos[pid] || !addr)
    return NULL;

  Dso *dso = pids.dsos[pid];
  while (dso) {
    if (addr >= dso->start && addr < dso->end)
      break;
    dso = dso->next;
  }
  return dso;
}

#define DSO_CACHE_MAX 256
DsoCache dso_cache[DSO_CACHE_MAX] = {0};
unsigned int i_dc = 0; // Insertion pointer

DsoCache *dso_cache_add(Dso *dso) {
  assert(dso);
  if (!dso)
    return NULL;

  // Try to open the region
  size_t sz = 1 + dso->end - dso->start;
  int fd = open(dso_path(dso), O_RDONLY);
  void *region = mmap(0, sz, PROT_READ, MAP_PRIVATE, fd, dso->pgoff);
  close(fd);
  if (MAP_FAILED == region)
    return NULL;

  // If there's already a region in the current position, close it
  if (dso_cache[i_dc].region) {
    munmap(dso_cache[i_dc].region, dso_cache[i_dc].sz);
    memset(&dso_cache[i_dc], 0, sizeof(DsoCache));
  }

  // Add it to the cache
  dso_cache[i_dc].filename = dso->filename;
  dso_cache[i_dc].pgoff = dso->pgoff;
  dso_cache[i_dc].sz = sz;
  dso_cache[i_dc].region = region;

  // Iterate
  i_dc = (i_dc + 1) & ~DSO_CACHE_MAX;

  return &dso_cache[i_dc];
}

DsoCache *dso_cache_find(Dso *dso) {
  assert(dso);
  if (!dso)
    return NULL;

  unsigned int i = (i_dc - 1) & ~DSO_CACHE_MAX; // Go backwards
  while (i != i_dc) {
    if (!memcmp(&dso->pgoff, &dso_cache[i],
                sizeof(uint32_t) + sizeof(uint64_t))) {
      // assert: sz and cache sz are the same because pgoff is a file-level
      //         offset and these segments are determined by the ELF header.
      //         If I'm wrong and these are variable-sized, then we should
      //         munmap this entry and add the larger size
      return &dso_cache[i];
    }
    i = (i - 1) & ~DSO_CACHE_MAX;
  }

  // If we didn't find it, let's populate the cache
  return dso_cache_add(dso);
}

bool pid_read(int pid, void *buf, size_t sz, uint64_t addr) {
  assert(pid > 0 && pid <= PID_MAX);
  assert(addr > 4095); // Zero page is a thing
  assert(buf);
  assert(sz > 0);

  // Our DSO lookup is fixed to process-space parameters, so even though the
  // read is conducted in file-space, the DSO lookup is in VM-space
  Dso *dso = dso_find(pid, addr);
  if (!dso)
    return false;

  // Find the cached segment
  DsoCache *dc = dso_cache_find(dso);
  if (!dc)
    return false;

  // Ensure we have enough headroom to read the requested amount
  if (addr + sz > dc->sz)
    return false;

  // Since addr is assumed in VM-space, convert it to segment-space, which is
  // file space minus the offset into the leading page of the segment
  if (addr < (dso->start + dso->pgoff)) {
    return false;
  }
  addr = addr - (dso->start + dso->pgoff);

  // At this point, we've
  //  Found a segment with matching parameters
  //  Adjusted addr to be a segment-offset
  //  Confirmed that the segment has the capacity to support our read
  // So let's read it!
  memcpy(dc->region + addr, buf, sz);
  return true;
}

#endif
