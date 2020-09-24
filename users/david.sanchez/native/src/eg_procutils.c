#include "procutils.h"

int main() {
  procfs_mapPrint(0);
  MapLine map = {0};
  hackptr ptr = {.fun = (void (*)(void))open};
  procfs_mapMatch(0, &map, ptr.num);
  return 0;
}
