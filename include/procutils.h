#ifndef _H_procutils
#define _H_procutils

#include <ctype.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// ISO C does not allow you to cast a function pointer to an object pointer.
// But it DOES allow you to cast a function pointer to a different, incompatible
// one, and unions don't care. Sometimes this is useful.
typedef union hackptr {
  void (*fun)(void);
  void *ptr;
  uint64_t num;
} hackptr;

typedef enum MapMode {
  PUMM_READ = 1 << 0,
  PUMM_WRITE = 1 << 1,
  PUMM_EXEC = 1 << 2,
  PUMM_COW = 1 << 3, // 0 if private (CoW), 1 if shared
  PUMM_HEAP = 1 << 4,
  PUMM_STACK = 1 << 5,
  PUMM_VDSO = 1 << 6,
  PUMM_ANON = 1 << 7, // Not a file and not special
  PUMM_SPECIAL = PUMM_STACK | PUMM_HEAP | PUMM_VDSO
} MapMode;

typedef struct Map {
  uint64_t start; // Start of the segment in virtual memory
  uint64_t end;   // End of the segment in virtual memory
  uint64_t off;   // Offset into the file of the segment
  char *path;     // path WITHIN THE PID MNT NS; has to be readjusted to caller
  MapMode mode;
  void *map; // an mmap() of the segment
} Map;

#define PM_MAX 128
typedef struct PidMap {
  pid_t pid;
  Map map[PM_MAX]; // TODO make this dynamic
  size_t n_map;    // How many are populated
} PidMap;

/*
 *  Global Map Cache
 *
 *  Table-encoded tree of PID relationships and corresponding maps.  If a pid
 * entry has an empty map, that means inherit from parent.
 */
#define MC_MAX 1024
typedef struct MapCache {
  pid_t pid[MC_MAX];   // pid->index reverse lookup; TODO make this dynamic
  PidMap maps[MC_MAX]; // TODO make this dynamic
  size_t sz;           // How many are populated
  MapMode whitelist;   // Disallow these types
} MapCache;

// Global map cache
MapCache g_mapcache = {0};

// TODO are these even necessary anymore?
char *g_procfs_linebuffer = NULL;
size_t g_procfs_linebuffer_sz = 0;

size_t mapcache_Find(pid_t);
void pidmap_Set(pid_t, PidMap *);
size_t mapcache_Set(pid_t);
PidMap *mapcache_Get(pid_t);
int procfs_MapOpen(pid_t target);
void procfs_MapPrint(Map *);
void procfs_PidMapPrint(pid_t);
Map *procfs_MapMatch(pid_t, uint64_t);
Map *procfs_MapScan(char *, Map *);

size_t mapcache_Find(pid_t pid) {
  size_t i = 0;
  for (i = 0; i < g_mapcache.sz; i++)
    if (pid == g_mapcache.pid[i] || !g_mapcache.pid[i])
      break;
  if (i == MC_MAX)
    printf("<HORRIBLE ERROR> mapcache ran out\n");
  return i;
}

void pidmap_Set(pid_t pid, PidMap *pm) {
  FILE *procstream = fdopen(procfs_MapOpen(pid), "r");
  if (!procstream) {
    printf("WTF!!! I couldn't find procfs for %d\n", pid);
    return;
  }
  pm->pid = pid;

  for (size_t i = 0; i < PM_MAX &&
       0 < getline(&g_procfs_linebuffer, &g_procfs_linebuffer_sz, procstream);
       i++) {
    if (!procfs_MapScan(g_procfs_linebuffer, &pm->map[i]))
      i--;
  }
}

void mapcache_MaskSet(MapMode whitelist) { g_mapcache.whitelist = whitelist; }

size_t mapcache_Set(pid_t pid) {
  size_t id = mapcache_Find(pid);
  g_mapcache.pid[id] = pid;

  pidmap_Set(pid, &g_mapcache.maps[id]);
  return id;
}

pid_t procfs_ppid(pid_t pid) {
  // NOTE, returns 0 on failure
  char filepath[1024] = {0};
  snprintf(filepath, 1023, "/proc/%d/status", pid);
  FILE *procstream = fopen(filepath, "r");
  if (!procstream)
    return 0;

  // TODO lol...
  getline(&g_procfs_linebuffer, &g_procfs_linebuffer_sz, procstream); // name
  getline(&g_procfs_linebuffer, &g_procfs_linebuffer_sz, procstream); // umask
  getline(&g_procfs_linebuffer, &g_procfs_linebuffer_sz, procstream); // state
  getline(&g_procfs_linebuffer, &g_procfs_linebuffer_sz, procstream); // tgid
  getline(&g_procfs_linebuffer, &g_procfs_linebuffer_sz, procstream); // ngid
  getline(&g_procfs_linebuffer, &g_procfs_linebuffer_sz, procstream); // pid
  getline(&g_procfs_linebuffer, &g_procfs_linebuffer_sz, procstream); // ppid

  return strtoull(g_procfs_linebuffer + strlen("ppid:"), NULL, 10);
}

PidMap *mapcache_Get(pid_t _pid) {
  pid_t pid = _pid;
  size_t id = mapcache_Find(pid);
  if (!g_mapcache.maps[id].pid) {
    pidmap_Set(pid, &g_mapcache.maps[id]);
    g_mapcache.pid[id] = pid;
  }
  return &g_mapcache.maps[id];
}

int g_procfs_map_fd = -1; // Probably do not want this...
pid_t g_procfs_map_pid = 0;

inline static char strsame_right(char *l, char *r) {
  return !strncmp(l, r, strlen(r));
}

int procfs_MapOpen(pid_t target) {
  if (target != g_procfs_map_pid) {
    if (-1 != g_procfs_map_fd)
      close(g_procfs_map_fd); // doesn't matter if -1, since we ignore
    g_procfs_map_fd = -1;
  }
  if (-1 == g_procfs_map_fd) {
    char buf[1024] = {0};
    snprintf(buf, 1024, "/proc/%d/maps", target);
    g_procfs_map_fd = open(buf, O_RDONLY);
  }
  if (-1 == g_procfs_map_fd) {
    // TODO, general logging
    printf("Error opening the procfs map\n");
    return -1;
  }

  return g_procfs_map_fd;
}

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// TODO, we're going to want a function that takes a path relative to a given
//       PID and returns the path in the mount namespace of the calling process.
char procfs_currentroot[sizeof("/proc/32768/root")] = {0};
char *procfs_RootGet(pid_t pid) {
  memset(procfs_currentroot, 0, sizeof(procfs_currentroot));
  snprintf(procfs_currentroot, sizeof(procfs_currentroot), "/proc/%d/root",
           pid);
  return procfs_currentroot;
}

char procfs_MmapGet(Map *map) {
  // If this segment hasn't been cached, then cache it.
  // TODO evict stale ones
  if (!map->map) {
    int fd = open(map->path, O_RDONLY);
    if (-1 == fd) {
      printf("I couldn't open the map (%s)!\n", map->path);
      return -1;
    }
    uint64_t mapsz = map->end - map->start + 1; // e.g., if start=end, map "1"b
    map->map = mmap(0, mapsz, PROT_READ, MAP_PRIVATE, fd, map->off);
    close(fd);
    if (!map->map) {
      printf("I couldn't map the map!\n");
      return -1;
    }
  }
  return 0;
}

ssize_t procfs_MapRead(Map *map, void *buf, size_t sz, size_t addr) {
  if (procfs_MmapGet(map))
    return -1;
  assert(map->map);

  // Out of bounds, don't even retry
  if (addr < (map->start - map->off) || addr >= (map->end - map->off - sz)) {
    return -1;
  }
  memcpy(buf, map->map + addr, sz);
  return 0;
}

void procfs_MapPrint(Map *map) {
  printf("<0x%lx, 0x%lx, 0x%lx> ", map->start, map->end, map->off);
  printf((PUMM_READ & map->mode) ? "r" : "-");
  printf((PUMM_WRITE & map->mode) ? "w" : "-");
  printf((PUMM_EXEC & map->mode) ? "x" : "-");
  printf((PUMM_COW & map->mode) ? "p" : "s");
  printf(" ");
  if (map->path)
    printf("%s", map->path);
  printf("\n");
}

void procfs_PidMapPrintProc(pid_t target) {
  if (!target)
    target = getpid();
  char path[4096] = {0};
  char _buf[4096] = {0};
  char *buf = _buf;
  size_t sz_buf = 4096;
  snprintf(path, 4095, "/proc/%d/maps", target);
  FILE *stream = fopen(path, "r");
  while (0 < getline(&buf, &sz_buf, stream)) {
    printf("%s", buf);
  }
  fclose(stream);
}

void procfs_PidMapPrint(pid_t target) {
  if (!target)
    target = getpid();

  PidMap *pm = mapcache_Get(target);
  size_t i = 0;
  while (pm->map[i].end) {
    procfs_MapPrint(&pm->map[i]);
    i++;
  }
}

Map *procfs_MapMatch(pid_t target, uint64_t addr) {
  size_t i = 0;

  if (!target)
    target = getpid();
  PidMap *pm = mapcache_Get(target);
  while (pm->map[i].end) {
    if (addr < pm->map[i].end) // Within bounds!
      return &pm->map[i];
    if (addr < pm->map[i].start)
      break; // This table is sorted
    i++;
  }

  mapcache_Set(target);
  return NULL;
}

Map *procfs_MapScan(char *line, Map *map) {
  uint64_t m_start = 0;
  uint64_t m_end = 0;
  uint64_t m_off = 0;
  char m_mode[4] = {0};
  int m_p = 0; // index into the line where the name starts

  if (4 !=
      sscanf(line, "%lx-%lx %4c %lx %*x:%*x %*d%n", &m_start, &m_end,
             &m_mode[0], &m_off, &m_p))
    return NULL;

  // Make sure the name index points to the first valid character
  char *p = &line[m_p], *q;
  while (' ' == *p)
    p++;
  if ((q = strchr(p, '\n')))
    *q = '\0';

  // Now save it
  switch (*p) {
  case '[':
  case '\0':
    return NULL;
  default:
    if (map->path)
      free(map->path);
    map->path = strdup(p);
    break;
  }

  map->start = m_start;
  map->end = m_end;
  map->off = m_off;
  map->mode = 0;
  for (int i = 0; i < 4; i++)
    switch (m_mode[i]) {
    case 'r':
      map->mode |= PUMM_READ;
      break;
    case 'w':
      map->mode |= PUMM_WRITE;
      break;
    case 'x':
      map->mode |= PUMM_EXEC;
      break;
    case 'p':
      map->mode |= PUMM_COW;
      break;
    case 's':
      break; // don't do anything, since this is just not 'p'
    }

  // Done
  return map;
}
#endif
