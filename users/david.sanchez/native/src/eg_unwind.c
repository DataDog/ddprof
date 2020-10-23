#include "unwind.h"

int main(void) {
  hackptr ptr = {.fun = (void(*)(void))open};

  {
    struct FunLoc* loc = &(struct FunLoc){0};
    process_ip(0, ptr.num, loc);
    printf("%s (%s:%d) [%s]\n", loc->funname, loc->srcpath, loc->line, loc->sopath);
  }

}
