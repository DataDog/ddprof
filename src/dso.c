#include "dso.h"

PidList pids = {0};
StringTable stab_dso = {0};
int dso_errno = 0;

void libdso_init() { stringtable_init(&stab_dso, NULL); }

char *dso_path(Dso *dso) {
  assert(dso);
  return (char *)stringtable_get(&stab_dso, dso->filename);
}

char *dsocache_path(DsoCache *dso) {
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
      // If we already have this one, skip
      if (dso_isequal(dso, in))
        return true;

      // If we're overlapping, then we have an invalid state.  The only thing
      // we can do now is return an error.  It's up to the user to determine
      // whether to invalidate and retry.
      if (dso_overlap(dso, in)) {
        LG_WRN("[DSO] invalid DSO overlap detected");
        return false;
      }
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

bool ip_in_procline(char *line, uint64_t ip) {
  static char spec[] = "%lx-%lx %4c %lx %*x:%*x %*d%n";
  uint64_t m_start = 0;
  uint64_t m_end = 0;
  uint64_t m_off = 0;
  char m_mode[4] = {0};
  int m_p = 0;

  if (4 != sscanf(line, spec, &m_start, &m_end, m_mode, &m_off, &m_p)) {
    LG_WRN("[DSO] Failed to scan mapfile line (search)");
    return false;
  }

  return ip >= m_start && ip <= m_end;
}

void pid_find_ip(int pid, uint64_t ip) {
  FILE *mpf = procfs_map_open(pid);
  if (!mpf) {
    if (process_is_alive(pid))
      LG_DBG("[DSO] Couldn't find ip:0x%lx for %d, process is dead", ip, pid);
    else
      LG_DBG("[DSO] Couldn't find ip:0x%lx for %d, mysteriously", ip, pid);
    return;
  }

  char *buf = NULL;
  size_t sz_buf = 0;
  while (-1 != getline(&buf, &sz_buf, mpf)) {
    if (ip_in_procline(buf, ip)) {
      LG_DBG("[DSO] Found ip:0x%lx for %d", ip, pid);
      LG_DBG("[DSO] %.*s", (int)strlen(buf) - 1, buf);
      fclose(mpf);
      return;
    }
  }

  LG_DBG("[DSO] Couldn't find ip:0x%lx for %d", ip, pid);
  fclose(mpf);
  return;
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

  if (4 != sscanf(line, spec, &m_start, &m_end, m_mode, &m_off, &m_p)) {
    LG_WRN("[DSO] Failed to scan mapfile line");
    return NULL;
  }

  // Make sure the name index points to a valid char
  char *p = &line[m_p], *q;
  while (isspace(*p))
    p++;
  if ((q = strchr(p, '\n')))
    *q = '\0';

  // Check that it was executable OR the stack
  if ('x' != m_mode[2] && strcmp(p, "[stack]"))
    return NULL;

  // OK, looks good
  out.addr = m_start;
  out.len = m_end - m_start;
  out.pgoff = m_off;
  out.filename = p;

  return &out;
}

bool pid_backpopulate(int pid) {
  LG_NTC("[DSO] Backpopulating PID %d", pid);
  FILE *mpf = procfs_map_open(pid);
  if (!mpf) {
    LG_WRN("[DSO] Failed to open procfs for %d", pid);
    if (process_is_alive(pid))
      LG_WRN("[DSO] Process nonexistant");
    return false;
  }

  char *buf = NULL;
  size_t sz_buf = 0;
  while (-1 != getline(&buf, &sz_buf, mpf)) {

    // TODO dso_from_procline should differentiate failures.  Non-ex is fine,
    //      parse error is bad
    DsoIn *out = dso_from_procline(buf);
    if (out && !pid_add(pid, out)) {
      LG_WRN("[DSO] Failed to add procline to PID %d", pid);
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
    LG_WRN("[DSO](FORK) No PPID found");
    if (!pid_backpopulate(ppid)) {
      LG_WRN("[DSO](FORK) No PID found");
      return pid_backpopulate(pid);
    }
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

  if (pid <= 0 || pid > PID_MAX) {
    dso_errno = DSO_EPID;
    return NULL;
  }
  if (!pids.dsos[pid]) {
    dso_errno = DSO_NOTFOUND;
    return NULL;
  }
  if (addr < 4095) {
    dso_errno = DSO_EADDR;
    return NULL;
  }

  Dso *dso = pids.dsos[pid];
  bool addr_gt = true;
  while (dso) {
    if (addr >= dso->start && addr < dso->end)
      break;
    if (addr <= dso->end)
      addr_gt = false;

    dso = dso->next;
  }

  // We need to be rather careful here.  perf events will give us executable
  // mappings, but sometimes DWARF unwinding seems to request data from
  // non-executable regions (error?).  Since perf events will drop events
  // under contention (can this be fixed for MMAP events?), we don't know a
  // priori whether
  // * An address is valid and cached
  // * An address is invalid (e.g., in the stack)
  // * An address is valid (executable), but hasn't been cached yet (dropped
  //   or out-of-order exec)
  // * An address WAS valid but is no longer (we'll give bad unwinding data)
  //
  // If we're here, we don't have an address.  We make the following simplifying
  // assumptions.
  // * It is very unlikely for a new region to be mapped below existing regions,
  //   since it is unlikely for regions to be unmapped (this assumption breaks
  //   down immediately for JIT...)
  // * If an address is within 1 GB of the top (well, bottom...) of the stack,
  //   we assume the address is in the stack
  //
  // Here we'll check for address monotonicity; in the caller we check for
  // stack
  if (!dso && addr_gt) {
    dso_errno = DSO_NOTFOUND;
    return NULL;
  }

  dso_errno = DSO_OK;
  return dso;
}

#define DSO_CACHE_MAX 256 // Actually a max ID
DsoCache dso_cache[DSO_CACHE_MAX] = {0};
unsigned int i_dc = 0; // Insertion pointer

DsoCache *dso_cache_add(Dso *dso) {
  assert(dso);
  if (!dso)
    return NULL;

  void *region = NULL;
  size_t sz = 0;
  // Is this a universal region?
  // TODO if we get more of these, refactor to lookup
  if ('[' == *dso_path(dso)) {
    if (!strcmp(dso_path(dso), "[vdso]")) {
      // So, I could parse procfs to figure out the vdso size and I could cache
      // it, but I notice on v5.4 `readelf` shows the dynamic section only has
      // size 0x4c0, so I'll round up to a page.  I checked multiple different
      // vDSO-enabled kernel distros and all of them were well under 4096B
      LG_DBG("[DSO] Found a VDSO region");
      region = (void *)(uintptr_t)getauxval(AT_SYSINFO_EHDR);
      sz = 4096;
    } else if (!strcmp(dso_path(dso), "[vsyscall]")) {
      // See Linux kernel sources at Documentation/x86/x86_64/mm.rst; this is a
      // totally standard value
      LG_DBG("[DSO] Found a VSYSCALL region");
      region = (void *)0xffffffffff600000;
      sz = 4096;
    } else {
      return NULL;
    }
  } else {
    // Try to open the region
    sz = 1 + dso->end - dso->start;
    int fd = open(dso_path(dso), O_RDONLY);
    region = mmap(0, sz, PROT_READ, MAP_PRIVATE, fd, dso->pgoff);
    close(fd);
    if (MAP_FAILED == region)
      return NULL;
  }

  // If there's already a region in the current position, close it
  if (dso_cache[i_dc].region) {
    if ('[' != *dsocache_path(&dso_cache[i_dc])) {
      LG_WRN("[DSO] clearing %d", i_dc);
      munmap(dso_cache[i_dc].region, dso_cache[i_dc].sz);
      memset(&dso_cache[i_dc], 0, sizeof(DsoCache));
    }
    memset(&dso_cache[i_dc], 0, sizeof(DsoCache)); // unnecessary
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

  unsigned int i = (i_dc + DSO_CACHE_MAX - 1) & ~DSO_CACHE_MAX; // Go backwards
  while (i != i_dc) {
    if (!memcmp(&dso->pgoff, &dso_cache[i],
                sizeof(uint32_t) + sizeof(uint64_t))) {
      // assert: sz and cache sz are the same because pgoff is a file-level
      //         offset and these segments are determined by the ELF header.
      //         If I'm wrong and these are variable-sized, then we should
      //         munmap this entry and add the larger size
      return &dso_cache[i];
    } else if (!dso_cache[i].region) {
      // Optimized processes won't have many regions mapped, so also check that
      // we haven't run out of ringbuffer
      break;
    }

    i = (i + DSO_CACHE_MAX - 1) & ~DSO_CACHE_MAX; // Go backwards
  }

  // If we didn't find it, let's populate the cache
  return dso_cache_add(dso);
}

bool pid_read_dso(int pid, void *buf, size_t sz, uint64_t addr) {
  assert(pid > 0 && pid <= PID_MAX);
  assert(buf);
  assert(sz > 0);

  // Can skip the zero page
  if (addr < 4095)
    return false;

  // Our DSO lookup is fixed to process-space parameters, so even though the
  // read is conducted in file-space, the DSO lookup is in VM-space
  Dso *dso = dso_find(pid, addr);
  if (!dso) {
    if (dso_errno == DSO_NOTFOUND) {
      LG_WRN("[DSO] Couldn't find DSO for [%d](0x%lx), stopping", pid, addr);
      return false;
    }

    // If we didn't find it, then try full population
    LG_WRN("[DSO] Couldn't find DSO for [%d](0x%lx)", pid, addr);
    pid_backpopulate(pid);
    if (!(dso = dso_find(pid, addr))) {
      pid_find_ip(pid, addr);
      return false;
    }
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
