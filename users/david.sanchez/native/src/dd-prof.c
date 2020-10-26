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

PPProfile* pprof = &(PPProfile){0};
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

struct DDProfContext {
  PPProfile* pprof;
  struct UnwindState* us;
  double sample_sec;
};


#define MAX_STACK 1024 // TODO what is the max?
                       // TODO harmonize max stack between here and unwinder
void rambutan_callback(struct perf_event_header* hdr, void* arg) {
  static uint64_t id_locs[MAX_STACK] = {0};
  static struct FunLoc locs[MAX_STACK] = {0};

  struct DDProfContext* pctx = arg;
  struct UnwindState* us = pctx->us;
  struct perf_event_sample* pes;
  struct timeval tv = {0};
  gettimeofday(&tv, NULL);
  int64_t now_nanos = (tv.tv_sec*1000000 + tv.tv_usec)*1000;
  switch(hdr->type) {
    case PERF_RECORD_SAMPLE:
      pes = (struct perf_event_sample*)hdr;
      us->pid = pes->pid;
      us->stack = pes->data;
      us->stack_sz = pes->dyn_size;
      memcpy(&us->regs[0], pes->regs, 3*sizeof(uint64_t));
      int n = unwindstate_unwind(us, locs);
      for(int i=0; i<n; i++) {
        memset(locs, 0, n*sizeof(*locs));
        memset(id_locs, 0, n*sizeof(*id_locs));
        uint64_t id_map = pprof_mapAdd(pprof, locs[i].map_start, locs[i].map_end, locs[i].map_off, locs[i].sopath, "");
        uint64_t id_fun = pprof_funAdd(pprof, locs[i].funname, locs[i].funname, locs[i].srcpath, locs[i].line);
        uint64_t id_loc = pprof_locAdd(pprof, id_map, locs[i].ip, (uint64_t[]){id_fun}, (int64_t[]){0}, 1);
        id_locs[i] = id_loc;
      }
      pprof_sampleAdd(pprof, (int64_t[]){1, pes->period}, 2, id_locs, n);
      break;

    default:
      break;
  }
  int64_t tdiff = (now_nanos - pprof->time_nanos)/1e9;
  printf("Time stuff: %ld\n", tdiff);
  if(pctx->sample_sec < tdiff) {
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
  pprof_Init(pprof, (char**)&(char*[]){"samples", "cpu"}, (char**)&(char*[]){"count", "nanoseconds"}, 2);
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
    struct DDProfContext rs = {
      .pprof = pprof,
      .us    = &(struct UnwindState){0}};
    unwindstate_Init(rs.us);
    elf_version(EV_CURRENT);
    main_loop(&pe, rambutan_callback, &rs);
  }

  return 0;
}
