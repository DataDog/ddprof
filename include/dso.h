#ifndef _H_dso
#define _H_dso

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "string_table.h"

// TODO this isn't good
#define PID_MAX 4194304
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

char *dso_path(Dso *dso) {
  assert(dso);
  return (char *)stringtable_get(&stab_dso, dso->filename);
}

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

bool dso_overlap(Dso *dso, DsoIn *in) {
  uint64_t l1 = dso->start, l2 = in->addr;
  uint64_t r1 = dso->end, r2 = in->addr + in->len;

  return l1 <= r2 && l2 <= r1;
}

bool dso_isequal(Dso *dso, DsoIn *in) {
  if (dso->start != in->addr)
    return false;
  if (dso->pgoff != in->pgoff)
    return false;

  // Get normalized names (just filename)
  char *str_left = dso_path(dso);
  char *str_right = in->filename;
  char *name_left = strrchr(str_left, '/');
  char *name_right = strrchr(str_right, '/');
  if (name_left)
    str_left = name_left;
  if (name_right)
    str_right = name_right;

  if (strcmp(str_left, str_right))
    return false;

  // OK, looks good
  return true;
}

bool pid_add(int pid, DsoIn *in) {
  assert(pid > 0 && pid <= PID_MAX);

  // Skip to last DSO, making sure we aren't duplicating what we have
  Dso *dso = pids.dsos[pid];

  if (!dso) {
    pids.dsos[pid] = malloc(sizeof(Dso));
    dso = pids.dsos[pid];
  } else {
    while (dso->next) {
      // If we're overlapping, then we have an invalid state.  The only thing
      // we can do now is return an error.  It's up to the user to determine
      // whether to invalidate and retry.
      if (dso_overlap(dso, in))
        return false;
      // If we already have this one, skip
      if (dso_isequal(dso, in))
        return true;
      dso = dso->next;
    }
    dso->next = malloc(sizeof(Dso));
    dso = dso->next;
  }

  // Populate
  dso->next = NULL;
  dso->start = in->addr;
  dso->end = in->addr + in->len;
  dso->pgoff = in->pgoff;

  ssize_t id_str = stringtable_add_cstr(&stab_dso, (const char *)in->filename);
  if (id_str >= 0)
    dso->filename = id_str;

  return true;
}

FILE *procfs_map_open(int pid) {
  char buf[1024] = {0};
  snprintf(buf, 1024, "/proc/%d/maps", pid);
  return fopen(buf, "r");
}

// Note that return could be invalidated if `line` changes
DsoIn *dso_from_procline(char *line) {
  static DsoIn out = {0};
  static char spec[] = "%lx-%lx %4c %lx %*x:%*x %*d%n";
  memset(&out, 0, sizeof(out));
  uint64_t m_start = 0;
  uint64_t m_end = 0;
  uint64_t m_off = 0;
  char m_mode[4] = {0};
  int m_p = 0;

  if (4 != sscanf(line, spec, &m_start, &m_end, m_mode, &m_off, &m_p))
    return NULL;

  // Check that it was executable
  if ('x' != m_mode[2])
    return NULL;

  // Make sure the name index points to a valid char
  char *p = &line[m_p], *q;
  while (isspace(*p))
    p++;
  if ((q = strchr(p, '\n')))
    *q = '\0';

  if ('[' == *p || !*p)
    return NULL;

  // OK, looks good
  out.addr = m_start;
  out.len = m_end - m_start;
  out.pgoff = m_off;
  out.filename = p;

  return &out;
}

bool pid_backpopulate(int pid) {
  FILE *mpf = procfs_map_open(pid);
  if (!mpf)
    return false;

  char *buf = NULL;
  size_t sz_buf = 0;
  while (getline(&buf, &sz_buf, mpf)) {
    DsoIn *out = dso_from_procline(buf);
    if (!out || !pid_add(pid, out)) {
      fclose(mpf);
      return false;
    }
  }

  fclose(mpf);
  return true;
}

bool pid_fork(int ppid, int pid) {
  assert(pid > 0 && pid <= PID_MAX);
  assert(ppid > 0 && ppid <= PID_MAX);

  // If we haven't seen the parent before, try to populate it and continue.  If
  // we can't do that, then try to just populate the child and return.
  if (!pids.dsos[ppid]) {
    if (!pid_backpopulate(ppid))
      return pid_backpopulate(pid);
  }

  // If the child already exists, then clear it
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

  return true;
}

Dso *dso_find(int pid, uint64_t addr) {
  assert(pid > 0);
  assert(pid <= PID_MAX);

  if (pid <= 0 || pid > PID_MAX || !pids.dsos[pid] || addr < 4095)
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
    printf("[CACHE] clearing %d\n", i_dc);
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

bool pid_read_dso(int pid, void *buf, size_t sz, uint64_t addr) {
  assert(pid > 0 && pid <= PID_MAX);
  assert(buf);
  assert(sz > 0);

  if (addr < 4095)
    return false;

  // Our DSO lookup is fixed to process-space parameters, so even though the
  // read is conducted in file-space, the DSO lookup is in VM-space
  Dso *dso = dso_find(pid, addr);
  if (!dso) {
    // If we didn't find it, then try full population
    pid_backpopulate(pid);
    if (!(dso = dso_find(pid, addr)))
      return false;
  }

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
