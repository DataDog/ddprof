#include <signal.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <unistd.h>

#include "unwind2.h"

int baz() {while(1){}; return 0;}
int bar() {return baz();}
int foo() {return bar();}

int main() {
  struct FunLoc locs[10] = {0};

  pid_t ppid = getpid();
  pid_t pid = 0;
  if ((pid = fork())) {
    struct UnwindState us = {.pid=pid};
    unwindstate__unwind(&us, locs, 256);
  } else {
    if (-1 == prctl(PR_SET_PDEATHSIG, SIGTERM))
      return -1;
    if (getppid() == 1) // If I've been orphaned, die
      return -1;
    foo();
  }
  return 0;
}
