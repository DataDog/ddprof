#include "ddog.h"

typedef union hackptr {
  void (*fun)(void);
  uint64_t num;
} hackptr;

int main() {
  DProf *dp      = &(DProf){0};
  dp->table_type = 1; // use string_table.h
  pprof_Init(dp, (const char **)&(const char *[]){"samples", "cpu"},
             (const char **)&(const char *[]){"count", "nanoseconds"}, 2);

  // Add some mapping stuff
  hackptr ptr[] = {{.fun = (void (*)(void))main},
                   {.fun = (void (*)(void))open},
                   {.fun = (void (*)(void))read}};

  pprof_sampleAdd(dp, (int64_t *)(int64_t[]){1, 100}, 2,
                  (uint64_t[]){ptr[0].num, ptr[1].num, ptr[2].num}, 3);
  pprof_sampleAdd(dp, (int64_t *)(int64_t[]){1, 100}, 2,
                  (uint64_t[]){ptr[0].num, ptr[1].num, ptr[2].num}, 3);
  pprof_sampleAdd(dp, (int64_t *)(int64_t[]){1, 100}, 2,
                  (uint64_t[]){ptr[0].num}, 1);

  // Connect and ship.  Don't initialize the DDRequest normally...
  DDRequest ddr = {.host    = "localhost",
                   .port    = "5556",
                   .key     = "1c77adb933471605ccbe82e82a1cf5cf",
                   .env     = "dev",
                   .version = "v0.1",
                   .service = "native-test-service",
                   .D       = &(Dictionary){0}};

  // TODO generate these better
  ddr_addtag(&ddr, "tags.host", "host:davebox");
  ddr_addtag(&ddr, "tags.service", "service:native-test-service");

  // Implementation stuff
  ddr_addtag(&ddr, "tags.prof_ver", "profiler-version:v0.1");
  ddr_addtag(&ddr, "tags.os", "runtime-os:linux-x86_64");

  // Language/runtime stuff
  ddr_addtag(&ddr, "tags.runtime", "runtime:native");
  ddr_addtag(&ddr, "tags.language", "language:native");
  ddr_addtag(&ddr, "runtime", "native");

  // Ship it!
  DDRequestSend(&ddr, dp);

  // Add more samples and redo
  //  pprof_sampleAdd(dp, (int64_t*)(int64_t[]){1, 100}, 2,
  //  (uint64_t[]){ptr[0].num, ptr[1].num}, 2); pprof_sampleAdd(dp,
  //  (int64_t*)(int64_t[]){1, 100}, 2, (uint64_t[]){ptr[0].num, ptr[1].num,
  //  ptr[2].num}, 3); DDRequestSend(&ddr, dp);
  return 0;
}
