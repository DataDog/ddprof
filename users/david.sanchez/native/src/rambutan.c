#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>

//#include <immintrin.h> // for __rdtsc();
//#include <x86intrin.h>

#include "perf.h"
#include "unwind.h"
#include "pprof.h"
#include "http.h"
#include "ddog.h"

PProf  _pprof = {0};
PProf* pprof = &_pprof;
Dict my_dict = {0};

DDRequest ddr = {
  .host = "localhost",
  .port = "8081",
//  .port = "5555",
  .key = "1c77adb933471605ccbe82e82a1cf5cf",
  .env = "dev",
  .version = "v0.1",
  .service = "native-test-service",
  .D = &my_dict
};

const int max_stack = 1024;
void rambutan_callback(struct perf_event_header* hdr, void* arg) {
  printf(".");
  unw_word_t ips[max_stack]; // TODO what is the max?
  struct perf_event_sample* pes;
  struct timeval tv = {0};
  gettimeofday(&tv, NULL);
  int64_t now_nanos = (tv.tv_sec*1000000 + tv.tv_usec)*1000;
//  PProf* pprof = (PProf*)arg;
  switch(hdr->type) {
    case PERF_RECORD_SAMPLE:
      ; // TODO
//      pes = (struct perf_event_sample*)hdr;
//      pprof_sampleAdd(pprof, 10000, pes->ips, pes->nr, pes->pid);
//      pprof_sampleAdd(pprof, pes->period, pes->data, pes->dyn_size, pes->pid);
//      unwind__get_entries(cb, arg, thread, data, 8192);
      struct UnwindState us = {
        .pid = pes->pid,
        .stack = pes->data,
        .stack_sz = pes->dyn_size,
        .regs = pes->regs
      };
      unwindstate_unwind(&us, ips, max_stack);
      break;

    default:
      break;
  }
  int64_t tdiff = (now_nanos - pprof->time_nanos)/1000000000;
  printf("Time stuff: %ld\n", tdiff);
  if(10 < tdiff) {
    DDRequestSend(&ddr, pprof);
  }
}

int main(int argc, char** argv) {
  char filename[128] = {0};
  char* fp = strrchr("/"__FILE__, '/')+1;
  memcpy(filename, fp, strlen(fp));
  (strrchr(filename, '.'))[0] = 0;

  if(argc==1) {
    printf("%s is a tool for getting stack samples from an application.  Please wrap your application in it.\n", filename);
    return -1;
  }

  // Initialize the pprof
  pprof_Init(pprof);
  pprof_timeUpdate(pprof); // Set the time

  // Finish initializing the DDR
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

  // Set the CPU affinity so that everything is on the same CPU.  Scream about
  // it because we want to undo this later..!
  cpu_set_t cpu_mask = {0};
  CPU_SET(0, &cpu_mask);
  if(!sched_setaffinity(getpid(), sizeof(cpu_set_t), &cpu_mask)) {
    printf("Successfully set the CPU mask.\n");
  } else {
    printf("Failed to set the CPU mask.\n");
    return -1;
  }

  // Setup a shared barrier for timing
  pthread_barrierattr_t bat = {0};
  pthread_barrier_t *pb = mmap(NULL, sizeof(pthread_barrier_t), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);

  pthread_barrierattr_init(&bat);
  pthread_barrierattr_setpshared(&bat, 1);
  pthread_barrier_init(pb, &bat, 2);

  // Fork, then run the child
  pid_t pid = fork();
  if(!pid) {
    pthread_barrier_wait(pb);
    munmap(pb, sizeof(pthread_barrier_t));
    execvp(argv[1], argv+1);
    printf("Hey, this shouldn't happen!\n");
    return -1;
  } else {
    PEvent pe = {0};
    char err =  perfopen(pid, &pe, NULL);
    if(-1 == err) {
      printf("Couldn't set up perf_event_open\n");
      return -1;
    }
    pthread_barrier_wait(pb);
    munmap(pb, sizeof(pthread_barrier_t));
//    main_loop(&pe, NULL, NULL);
    main_loop(&pe, rambutan_callback, (void*)pprof);
  }

  return 0;
}
