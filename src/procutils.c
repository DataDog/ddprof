#include "procutils.h"

#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// TODO, these probably aren't too necessary now...
char *g_procfs_linebuffer = NULL;
size_t g_procfs_linebuffer_sz = 0;

// Global map cache
MapCache g_mapcache = {0};

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
  if (!procstream)
    return;
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
  if (!map)
    return -1;
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
Map *current_map = NULL;
size_t current_addr = 0;
ssize_t procfs_MapRead(Map *map, void *buf, size_t sz, size_t addr) {
  if (!map)
    return -1;
  if (procfs_MmapGet(map))
    return -1;
  assert(map->map);

  // Checkpoint globals, to inspect during segfaults
  current_map = map;
  current_addr = addr;

  // Out of bounds, don't even retry
  if (addr < (map->start - map->off) || addr >= (map->end - map->off - sz)) {
    return -1;
  }
  memcpy(buf, (char *)map->map + addr, sz);

  // Restore globals
  current_map = NULL;
  current_addr = 0;
  return 0;
}

void procfs_MapPrint(Map *map) {
  if (!map) {
    printf("INVALID MAP\n");
    return;
  }
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
  while (i < PM_MAX && pm->map[i].end) {
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

static char StatusLine[] =
    "%d %s %c %d %d %d %d %u %lu %lu %lu %lu %lu %ld %ld %ld %ld %ld %ld "
    "%llu %lu %ld %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %d %d %u "
    "%u %llu %lu %ld %lu %lu %lu %lu %lu %lu %d";

DDRes proc_read(ProcStatus *procstat) {
  FILE *ststream = fopen("/proc/self/stat", "r");
  if (!ststream) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PROCSTATE, "Failed to open /proc/self/stat");
  }

  if (0 > fscanf(ststream, StatusLine, &procstat->pid, &procstat->comm,
                 &procstat->state, &procstat->ppid, &procstat->pgrp,
                 &procstat->session, &procstat->tty_nr, &procstat->tpgid,
                 &procstat->flags, &procstat->minflt, &procstat->cminflt,
                 &procstat->majflt, &procstat->cmajflt, &procstat->utime,
                 &procstat->stime, &procstat->cutime, &procstat->cstime,
                 &procstat->priority, &procstat->nice, &procstat->num_threads,
                 &procstat->itrealvalue, &procstat->starttime, &procstat->vsize,
                 &procstat->rss, &procstat->rsslim, &procstat->startcode,
                 &procstat->endcode, &procstat->startstack, &procstat->kstkesp,
                 &procstat->kstkeip, &procstat->signal, &procstat->blocked,
                 &procstat->sigignore, &procstat->sigcatch, &procstat->wchan,
                 &procstat->nswap, &procstat->cnswap, &procstat->exit_signal,
                 &procstat->processor, &procstat->rt_priority,
                 &procstat->policy, &procstat->delayacct_blkio_ticks,
                 &procstat->guest_time, &procstat->cguest_time,
                 &procstat->start_data, &procstat->end_data,
                 &procstat->start_brk, &procstat->arg_start, &procstat->arg_end,
                 &procstat->env_start, &procstat->env_end)) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PROCSTATE, "Failed to read /proc/self/stat");
  }
  fclose(ststream);
  return ddres_init();
}
