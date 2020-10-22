#include "unwind.h"

int main(void) {
  hackptr ptr = {.fun = (void(*)(void))open};
  pid_t pid = 0;
  struct FunLocLookup* flu = &(struct FunLocLookup){.loc = &(struct FunLoc){0}};

  Map* map = procfs_MapMatch(pid, ptr.num);
  uint64_t addr[] = {ptr.num - map->start +  map->off};
  size_t naddr = 1;
  flu->loc->sopath = map->path;
  process_file(flu, NULL, addr, naddr);
}
