#include "procutils.h"

int main() {
  procfs_MapPrint(0);
  hackptr ptr = {.fun = (void (*)(void))open};
  Map *map = procfs_MapMatch(0, ptr.num);
  if (!map)
    printf("Match not found!\n");
  else
    printf("Match in %s\n", map->path);
  return 0;
}
