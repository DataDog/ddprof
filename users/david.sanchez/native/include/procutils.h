#ifndef _H_procutils
#define _H_procutils

#include <ctype.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <gelf.h> // apt install libelf-dev


// ISO C does not allow you to cast a function pointer to an object pointer.
// But it DOES allow you to cast a function pointer to a different, incompatible
// one, and unions don't care. Sometimes this is useful.
typedef union hackptr {
  void   (*fun)(void);
  void*    ptr;
  uint64_t num;
} hackptr;

typedef enum MapMode {
  PUMM_READ  = 1<<0,
  PUMM_WRITE = 1<<1,
  PUMM_EXEC  = 1<<2,
  PUMM_COW   = 1<<3,  // 0 if private (CoW), 1 if shared
  PUMM_HEAP  = 1<<4,
  PUMM_STACK = 1<<5,
  PUMM_VDSO  = 1<<6,
  PUMM_ANON  = 1<<7,  // Not a file and not special
  PUMM_SPECIAL = PUMM_STACK | PUMM_HEAP | PUMM_VDSO
} MapMode;

typedef struct Map {
  uint64_t start;
  uint64_t end;
  uint64_t off;
  char*    path;
  MapMode  mode;
} Map;

#define PM_MAX 128
typedef struct PidMap {
  pid_t  pid;
  Map    map[PM_MAX]; // TODO make this dynamic
  size_t n_map;       // How many are populated
} PidMap;


/*
 *  Global Map Cache
 *
 *  Table-encoded tree of PID relationships and corresponding maps.  If a pid entry has an empty map,
 *  that means inherit from parent.
 */
#define MC_MAX 1024
typedef struct MapCache {
  pid_t   pid[MC_MAX];  // pid->index reverse lookup; TODO make this dynamic
  pid_t   ppid[MC_MAX]; // parent lookup if map is empty (inherited)
  PidMap  maps[MC_MAX]; // TODO make this dynamic
  size_t  sz;           // How many are populated
  MapMode whitelist;    // Disallow these types
} MapCache;
MapCache g_mapcache = {0};

// TODO are these even necessary anymore?
char*  g_procfs_linebuffer    = NULL;
size_t g_procfs_linebuffer_sz = 0;


size_t mapcache_Find(pid_t);
void   pidmap_Set(pid_t, PidMap*);
void   pidmap_SetFiltered(pid_t, PidMap*, MapMode);
size_t mapcache_Set(pid_t, pid_t);
char   procfs_LineToMap(char*, Map*);
char   procfs_LineToMapFiltered(char*, Map*, MapMode);
int    procfs_MapOpen(pid_t target);
void   procfs_MapPrint(pid_t);
Map*   procfs_MapMatch(pid_t, uint64_t);


size_t mapcache_Find(pid_t pid) {
  size_t i = 0;
  for(i=0; i<g_mapcache.sz; i++)
    if(pid == g_mapcache.pid[i] || !g_mapcache.pid[i])
      break;
  if(i == MC_MAX)
    printf("<HORRIBLE ERROR> mapcache ran out\n");
  return i;
}

void pidmap_Set(pid_t pid, PidMap* pm) {
  pidmap_SetFiltered(pid, pm, 0x0);
  return;
}

void pidmap_SetFiltered(pid_t pid, PidMap* pm, MapMode whitelist) {
  FILE* procstream = fdopen(procfs_MapOpen(pid), "r");
  if(!procstream) {
    printf("WTF!!! I couldn't find procfs for %d\n", pid);
    return;
  }
  pm->pid = pid;

  for(size_t i; i<PM_MAX && 0<getline(&g_procfs_linebuffer, &g_procfs_linebuffer_sz, procstream); i++) {
    if(procfs_LineToMapFiltered(g_procfs_linebuffer, &pm->map[i], whitelist)) // Couldn't process this line for some reason.  Skip it
      i--;
  }
}

void mapcache_MaskSet(MapMode whitelist) {
  g_mapcache.whitelist = whitelist;
}

size_t mapcache_Set(pid_t pid, pid_t ppid) {
  if(!g_mapcache.whitelist)
    mapcache_MaskSet(PUMM_EXEC);

  size_t id = mapcache_Find(pid);
  g_mapcache.pid[id]  = pid;
  g_mapcache.ppid[id] = ppid;

  pidmap_SetFiltered(pid, &g_mapcache.maps[id], g_mapcache.whitelist);
  return id;
}

pid_t procfs_ppid(pid_t pid) {
  // NOTE, returns 0 on failure
  char filepath[1024] = {0};
  snprintf(filepath, 1023, "/proc/%d/status", pid);
  FILE* procstream = fopen(filepath, "r");
  if(!procstream)
    return 0;

  // TODO lol...
  getline(&g_procfs_linebuffer, &g_procfs_linebuffer_sz, procstream); // name
  getline(&g_procfs_linebuffer, &g_procfs_linebuffer_sz, procstream); // umask
  getline(&g_procfs_linebuffer, &g_procfs_linebuffer_sz, procstream); // state
  getline(&g_procfs_linebuffer, &g_procfs_linebuffer_sz, procstream); // tgid
  getline(&g_procfs_linebuffer, &g_procfs_linebuffer_sz, procstream); // ngid
  getline(&g_procfs_linebuffer, &g_procfs_linebuffer_sz, procstream); // pid
  getline(&g_procfs_linebuffer, &g_procfs_linebuffer_sz, procstream); // ppid

  return strtoull(g_procfs_linebuffer+strlen("ppid:"), NULL, 10);
}

PidMap* mapcache_Get(pid_t _pid) {
  pid_t pid = _pid;
  size_t _id = mapcache_Find(pid), id=_id;
  do {
    if(g_mapcache.maps[id].pid) // ASSERT: pid populated IFF map is populated
      break;

    // Didn't find it.  Try the parent
    if(!g_mapcache.pid[id]) {
      id = mapcache_Set(_pid, procfs_ppid(_pid)); // Does another pointless Find operation
      break;
    }
    pid = g_mapcache.pid[id];
    id = mapcache_Find(pid);
  } while(1);

  return &g_mapcache.maps[id];
}

int   g_procfs_map_fd  = -1;  // Probably do not want this...
pid_t g_procfs_map_pid = 0;

inline static char strsame_right(char* l, char* r) {
  return !strncmp(l, r, strlen(r));
}

char procfs_LineToMap(char* line, Map* map) {
  return procfs_LineToMapFiltered(line, map, 0x0);
}

char procfs_LineToMapFiltered(char* line, Map* map, MapMode whitelist) {
  // ASSERT:  we have a complete line, which should be guaranteed by read() from
  // the procfs interface
  uint64_t addr_start, addr_end, offset;
  MapMode mask;
  char* filename = NULL;
  char *p=line, *q=line; // left and right cursors
  if(!line)
    return -1;

  // Process address
  addr_start = strtoull(p, &q, 16); if(p==q) {return -1;} p=++q; // p,q now point to first digit of next addr
  addr_end   = strtoull(p, &q, 16); if(p==q) {return -1;} p=++q; // p,q now point to first char of perms (not space)

  // Process perms.  For simplicitly, assume the perm flags have no ordering (i.e., characters are not position-sensitive)
  for(int i=0; i<4; i++,q++) {
    switch(*q) {
    case 'r': mask |= PUMM_READ;  break;
    case 'w': mask |= PUMM_WRITE; break;
    case 'x': mask |= PUMM_EXEC;  break;
    case 'p': mask |= PUMM_COW;   break;
    case 's':                     break; // don't do anything, since this is just not 'p'
    }
  }

  // Check that the mask has some bits in common with the whitelist
  if(!(mask&whitelist))
    return -1;
  p=++q; // p,q now points to first character of offset

  // Address offset into underlying file
  offset = strtoull(p, &q, 16); if(p==q) {return -1;} p=++q; // p,q now point to first char of device

  // At this point, we don't care about the inode or the device (maybe we should?), so just work backward from the
  // nearest newline
  if(!(q=strchr(q, '\n'))) {return -1;} // q now points to end of line
  if(!(q=strrchr(line,' '))) {return -1;} q++; // q now points to start of pathname
  if(strsame_right(q, "[stack")) {
    mask |= PUMM_STACK;
  } else if(strsame_right(q, "[heap")) {
    mask |= PUMM_HEAP;
  } else if(strsame_right(q, "[vdso")) {
    mask |= PUMM_VDSO;
  } else if(strsame_right(q, "[vvar")) {
  } else if(strsame_right(q, "[vsyscall")) {
  } else if(*q != '\n') {
    filename = strdup(q);
    *strrchr(filename, '\n') = 0; // knock out trailing newline
  }

  // We have everything we need!  First, cleanup potentially stale data
  if(map->path)
    free(map->path);
  memset(map, 0, sizeof(*map));

  // Populate the map
  map->start  = addr_start;
  map->end    = addr_end;
  map->off    = offset;
  map->mode   = mask;
  map->path   = filename;
printf("DBG: filename(%s)\n", filename);
  return 0;
}

int procfs_MapOpen(pid_t target) {
  if(target != g_procfs_map_pid) {
    close(g_procfs_map_fd);  // doesn't matter if -1, since we ignore
    g_procfs_map_fd = -1;
  }
  if(-1 == g_procfs_map_fd) {
    char buf[1024] = {0};
    snprintf(buf, 1024, "/proc/%d/maps", target);
    g_procfs_map_fd = open(buf, O_RDONLY);
  }
  if(-1 == g_procfs_map_fd) {
    // TODO, general logging
    printf("Error opening the procfs map\n");
    return -1;
  }

  return g_procfs_map_fd;
}

ssize_t procfs_MapRead(Map* map, void* buf, size_t off, size_t sz) {
  int fd = open(map->path, O_RDONLY);
  lseek(fd, off, SEEK_SET);
  ssize_t ret = read(fd, buf, sz);
  close(fd);
  return ret;
}

void procfs_MapPrint(pid_t target) {
  if(!target)
    target = getpid();

  PidMap* pm = mapcache_Get(target);
  size_t i = 0;
  while(pm->map[i].end) {
    Map* map = &pm->map[i];
    printf("<0x%lx, 0x%lx, 0x%lx> ", map->start, map->end, map->off);
    printf((PUMM_READ  & map->mode) ? "r" : "-");
    printf((PUMM_WRITE & map->mode) ? "w" : "-");
    printf((PUMM_EXEC  & map->mode) ? "x" : "-");
    printf((PUMM_COW   & map->mode) ? "p" : "s");
    printf(" ");
    if(map->path)
      printf("%s", map->path);
    printf("\n");

    i++;
  }
}

Map* procfs_MapMatch(pid_t target, uint64_t addr) {
  if(!target)
    target = getpid();

  PidMap* pm = mapcache_Get(target);
  size_t i = 0;
  while(pm->map[i].end) {
    if(addr >= pm->map[i].start && addr <= pm->map[i].end)
      return &pm->map[i];
    i++;
  }
  return NULL;
}

#endif
