#include "ddog.h"

int main() {
  PProf* pprof = &(PProf){0};
  pprof_Init(pprof);
  pprof_timeUpdate(pprof);
  pprof->period = 10000000;


  // Add some mapping stuff
  hackptr ptr[] = {
    {.fun = (void(*)(void))main},
    {.fun = (void(*)(void))open},
    {.fun = (void(*)(void))read}};

  pprof_sampleAdd(pprof, 100, (uint64_t[]){ptr[0].num, ptr[1].num, ptr[2].num}, 3, 0);
  pprof_sampleAdd(pprof, 1000, (uint64_t[]){ptr[0].num, ptr[1].num, ptr[2].num}, 3, 0);
  pprof_sampleAdd(pprof, 10000, (uint64_t[]){ptr[0].num}, 1, 0);

  // Connect and ship.  Don't initialize the DDRequest normally...
  DDRequest ddr = {
    .host = "localhost",
    .port = "5556",
    .key  = "1c77adb933471605ccbe82e82a1cf5cf",
    .env  = "dev",
    .version = "v0.1",
    .service = "native-test-service",
    .D = &(Dict){0}
  };

  // TODO generate these better
  ddr_addtag(&ddr, "tags.host",     "host:davebox");
  ddr_addtag(&ddr, "tags.service",  "service:native-test-service");

  // Implementation stuff
  ddr_addtag(&ddr, "tags.prof_ver", "profiler-version:v0.1");
  ddr_addtag(&ddr, "tags.os",       "runtime-os:linux-x86_64");

  // Language/runtime stuff
  ddr_addtag(&ddr, "tags.runtime",  "runtime:native");
  ddr_addtag(&ddr, "tags.language", "language:native");
  ddr_addtag(&ddr, "runtime", "native");

  // Ship it!
  DDRequestSend(&ddr, pprof);

  // Add more samples and redo
  pprof_sampleAdd(pprof, 100000, (uint64_t[]){ptr[0].num, ptr[1].num}, 2, 0);
  pprof_sampleAdd(pprof, 1000000, (uint64_t[]){ptr[0].num, ptr[1].num, ptr[2].num}, 3, 0);
  DDRequestSend(&ddr, pprof);
  return 0;
}
