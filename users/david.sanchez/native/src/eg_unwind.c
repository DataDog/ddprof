#include "unwind.h"

int main(void) {
  hackptr ptr = {.fun = (void (*)(void))open};

  int n = unwindstate_unwind(us, locs, 4096);
  for (int i = 0; i < n; i++) {}
}
