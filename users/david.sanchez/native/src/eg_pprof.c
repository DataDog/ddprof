#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <x86intrin.h>

#include "pprof.h"

int main() {
  Perftools__Profiles__Profile *pprof = &g_dd_pprofs[1];
  pprof_init(pprof);

  // Add some fake samples
  pprof_sampleAdd(pprof, 100, (uint64_t[]){1000, 2000}, 2);
  pprof_sampleAdd(pprof, 150, (uint64_t[]){1000, 2000}, 2);
  pprof_sampleAdd(pprof, 125, (uint64_t[]){1000, 2000}, 2);


  // Serialize and ship
  size_t len = perftools__profiles__profile__get_packed_size(pprof);
  void* buf = calloc(1,len);
  perftools__profiles__profile__pack(pprof, buf);
  printf("I have %ld bytes.\n", len);
  int fd = open("./test.pb", O_WRONLY|O_CREAT, 0777);
  ftruncate(fd, 0);
  write(fd, buf, len);
  close(fd);
  GZip("./test.pb.gz", buf, len);
  free(buf);
  pprof_free(pprof);
  return 0;
}
