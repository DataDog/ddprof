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


// ISO C does not allow you to cast a function pointer to an object pointer.
// But it DOES allow you to cast a function pointer to a different, incompatible
// one, and unions don't care. Sometimes this is useful.
typedef union hackptr {
  void   (*fun)(void);
  void*    ptr;
  uint64_t num;
} hackptr;

typedef struct MapLine {
  char     isFile;
  uint64_t start;
  uint64_t end;
  uint64_t off;
  char*    path;
} MapLine;

int   g_procfs_map_fd  = -1;  // Probably do not want this...
pid_t g_procfs_map_pid = 0;

char procfs_mapLineConvert(char* line, MapLine* map) {
  char *p=line, *q=line; // left and right cursors
  if(!line)
    return -1;

  // Process address
  uint64_t addr_start = strtoll(p, &q, 16); if(p==q) {return -1;} p=++q; // p,q now point to first digit of next addr
  uint64_t addr_end   = strtoll(p, &q, 16); if(p==q) {return -1;} p=++q; // p,q now point to first char of perms (not space)

  if(!(q=strchr(q,' '))) {return -1;} p=++q; // p,q now point to first character of offset
  uint64_t offset = strtoll(p, &q, 16); if(p==q) {return -1;} p=++q; // p,q now point to first char of device

  // Extract the pathname
  if(!(q=strrchr(line,' '))) {return -1;} q++; // q now points to start of pathname

  // We have everything we need!
  map->start  = addr_start;
  map->end    = addr_end;
  map->off    = offset;
  if(*q != '\n') { // if no path in line, q is now the end of the string, which is probably a newline
    map->path   = strdup(q);
    map->isFile = '/'==*map->path;
    char* w = strrchr(map->path, '\n');
    *w = 0; // knock out that trailing newline
  } else {
    map->path = NULL;
    map->isFile = 0;
  }
  return 0;
}

int procfs_mapOpen(pid_t target) {
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

char*  g_procfs_linebuffer    = NULL;
size_t g_procfs_linebuffer_sz = 0;

void procfs_mapPrint(pid_t target) {
  if(!target)
    target = getpid();

  FILE* procstream = fdopen(procfs_mapOpen(target), "r");
  static MapLine retbuf;

  while(0 < getline(&g_procfs_linebuffer, &g_procfs_linebuffer_sz, procstream)) {
    if(procfs_mapLineConvert(g_procfs_linebuffer , &retbuf))
      continue;
    if(retbuf.path)
      printf("<%ld,%ld> %s\n", retbuf.start, retbuf.end, retbuf.path);
  }

  fclose(procstream);
}

char procfs_mapMatch(pid_t target, MapLine* ret, uint64_t addr) {
  if(!target)
    target = getpid();

  FILE* procstream = fdopen(procfs_mapOpen(target), "r");
  static MapLine retbuf;
  int rc = -1;

  while(0 < getline(&g_procfs_linebuffer, &g_procfs_linebuffer_sz, procstream)) {
    if(procfs_mapLineConvert(g_procfs_linebuffer , &retbuf))
      continue;
    if(addr && addr < retbuf.start || addr > retbuf.end)
      continue;

    // If we're here, we found a valid address!
    // Note:  0 is a special address that means "return the first entry"
    memcpy(ret, &retbuf, sizeof(retbuf));
    rc = 0; // set the return code to success
    break;
  }
  fclose(procstream);
  return rc;
}


#endif
