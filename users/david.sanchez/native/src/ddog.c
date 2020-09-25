#include "ddog.h"

void ddr_addtag(DDRequest* ddr, char* tag, char* val) {
  DictSet(ddr->D, tag, val, 1+strlen(val));
}

int main() {
  PProf* pprof = &(PProf){0};
  pprof_Init(pprof);

  // Add some mapping stuff
  hackptr ptr[] = {
    {.fun = (void(*)(void))main},
    {.fun = (void(*)(void))open},
    {.fun = (void(*)(void))read}};

  pprof_sampleAdd(pprof, 100, (uint64_t[]){ptr[0].num, ptr[1].num, ptr[2].num}, 3);
  pprof_sampleAdd(pprof, 1000, (uint64_t[]){ptr[0].num, ptr[1].num, ptr[2].num}, 3);
  pprof_sampleAdd(pprof, 10000, (uint64_t[]){ptr[0].num}, 1);
  pprof_sampleAdd(pprof, 100000, (uint64_t[]){ptr[0].num, ptr[1].num}, 2);
  pprof_sampleAdd(pprof, 1000000, (uint64_t[]){ptr[0].num, ptr[1].num, ptr[2].num}, 3);

  // Connect and ship.  Don't initialize the DDRequest normally...
  DDRequest ddr = {
    .host = "localhost",
    .port = "8081",
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
  ddr_addtag(&ddr, "tags.runtime",  "runtime:go");
  ddr_addtag(&ddr, "tags.language", "language:go");
  ddr_addtag(&ddr, "runtime", "go");

  // Ship it!
  DDRequestSend(&ddr, pprof);
  return 0;
}
