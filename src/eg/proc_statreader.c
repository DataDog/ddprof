#include "procutils.h"

void main() {
  ProcStatus *procstat = proc_read();
  printf("pid: %d\n", procstat->pid);
  printf("rss: %ld\n", procstat->rss);
  printf("user: %ld\n", procstat->utime);
  printf("cuser: %ld\n", procstat->cutime);
}
