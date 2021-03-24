#include <getopt.h>
#include <libelf.h>
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "dd_send.h"
#include "http.h"
#include "perf.h"
#include "pprof.h"
#include "unwind.h"


#define max_watchers 10

typedef struct PerfOption {
  char key;
  int type;
  int config;
  int base_rate;
  char* label;
  char* unit;
} PerfOption;

struct DDProfContext {
  DProf *dp;
  DDReq *ddr;

  // Parameters for interpretation
  char *agent_host;
  char *prefix;
  char *tags;

  // Input parameters
  char *enabled;
  char *upload_period;
  char *profprofiler;
  struct {
    bool enabled;
    double upload_period;
    bool profprofiler;
  } params;
  struct watchers {
    PerfOption* opt;
    uint64_t sample_period;
  } watchers[max_watchers];
  int num_watchers;

  struct UnwindState *us;
  int64_t send_nanos;
};

PerfOption perfoptions[] = {
  // Hardware
  {'C', PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES,              1e6, "cpu-cycle",    "cycles"},
  {'R', PERF_TYPE_HARDWARE, PERF_COUNT_HW_REF_CPU_CYCLES,          1e6, "cpu-cycle",    "cycles"},
  {'I', PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS,            1e6, "cpu-instr",    "instructions"},
  {'H', PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES,        1e3, "cache-ref",    "events"},
  {'M', PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES,            1e3, "cache-miss",   "events"},
  {'P', PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS,     1e3, "branch-instr", "events"},
  {'Q', PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES,           1e3, "branch-miss",  "events"},
  {'B', PERF_TYPE_HARDWARE, PERF_COUNT_HW_BUS_CYCLES,              1e3, "bus-cycle",    "cycles"},
  {'F', PERF_TYPE_HARDWARE, PERF_COUNT_HW_STALLED_CYCLES_FRONTEND, 1e3, "bus-stf",      "cycles"},
  {'S', PERF_TYPE_HARDWARE, PERF_COUNT_HW_STALLED_CYCLES_BACKEND,  1e3, "bus-stb",      "cycles"},

  // Software
  {'c', PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK,              1e6, "cpu-time",     "nanoseconds"},
  {'w', PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_CLOCK,  1e6, "wall-time", "nanoseconds"},

  // Kernel tracepoints
  {'r', PERF_TYPE_TRACEPOINT, 1132,                   1,   "wall-time", "nanoseconds"},
};

int num_cpu = 0;

// clang-format off
/*
    This table is used for a variety of things, but primarily for dispatching
    input in a consistent way across the application.  Values may come from one
    of several places, with defaulting in the following order:
      1. Commandline argument
      2. Environment variable
      3. Application default

    And input may go to one of many places
      1. Profiling parameters
      2. User data annotations
      3. Upload parameters

  A - The enum for this value.  Also doubles as an environment variable whenever
      appropriate
  B - The field name.  It is the responsibility of other conumsers to combine
      this value with context to use the right struct
  C - Short option for input processing
  D - Long option
  E - Destination struct (WOW, THIS IS HORRIBLE)
  F - defaulting function (or NULL)
  G - Fallback value, if any
*/
#define X_LOPT(a, b, c, d, e, f, g) {#b, d, 0, *#c},
#define X_ENUM(a, b, c, d, e, f, g) a;
#define X_OSTR(a, b, c, d, e, f, g) #c ":"
#define X_DFLT(a, b, c, d, e, f, g) DFLT_EXP(#a, b, e, f, g);
#define X_CASE(a, b, c, d, e, f, g) case *#c: (e)->b = optarg; break;
#define X_PRNT(a, b, c, d, e, f, g) printf(#b ": %s\n", (e)->b);

//  A                            B                C  D  E         F     G
#define OPT_TABLE(X)                                                                 \
  X(DD_API_KEY,                  apikey,          A, 1, ctx->ddr, NULL, NULL)        \
  X(DD_ENV,                      environment,     E, 1, ctx->ddr, NULL, NULL)        \
  X(DD_AGENT_HOST,               host,            H, 1, ctx->ddr, NULL, "localhost") \
  X(DD_SITE,                     site,            I, 1, ctx->ddr, NULL, NULL)        \
  X(DD_TRACE_AGENT_PORT,         port,            P, 1, ctx->ddr, NULL, "8081")      \
  X(DD_SERVICE,                  service,         S, 1, ctx->ddr, NULL, "my_profiled_service") \
  X(DD_TAGS,                     tags,            T, 1, ctx,      NULL, NULL)        \
  X(DD_VERSION,                  profiler_version,V, 1, ctx->ddr, NULL, NULL)        \
  X(DD_PROFILING_ENABLED,        enabled,         d, 1, ctx,      NULL, "yes")       \
  X(DD_PROFILING_UPLOAD_PERIOD,  upload_period,   u, 1, ctx,      NULL, "60.0")      \
  X(DD_PROFILE_NATIVEPROFILER,   profprofiler,    p, 0, ctx,      NULL, NULL)        \
  X(DD_PROFILING_,               prefix,          X, 1, ctx,      NULL, "")
// clang-format off

#define DFLT_EXP(evar, key, targ, func, dfault)                                \
  ({                                                                           \
    char *_buf = NULL;                                                         \
    if (!((targ)->key)) {                                                      \
      if (evar && getenv(evar))                                                \
        _buf = getenv(evar);                                                   \
      else if (dfault)                                                         \
        _buf = strdup(dfault);                                                 \
      (targ)->key = _buf;                                                      \
    }                                                                          \
  })


/******************************  Perf Callback  *******************************/
static inline int64_t now_nanos() {
  static struct timeval tv = {0};
  gettimeofday(&tv, NULL);
  return (tv.tv_sec * 1000000 + tv.tv_usec) * 1000;
}

void ddprof_callback(struct perf_event_header *hdr, int pos, void *arg) {
  static uint64_t id_locs[MAX_STACK]   = {0};

  // TODO this time stuff needs to go into the struct
  static int64_t last_time = 0;
  if(!last_time) last_time = now_nanos();

  struct DDProfContext *pctx = arg;
  struct UnwindState *us     = pctx->us;
  DDReq *ddr                 = pctx->ddr;
  DProf *dp                  = pctx->dp;
  struct perf_event_sample *pes;
  switch (hdr->type) {
  case PERF_RECORD_SAMPLE:
    pes          = (struct perf_event_sample *)hdr;
    us->pid      = pes->pid;
    us->idx      = 0;
    us->stack    = pes->data;
    us->stack_sz = pes->size; // TODO should be dyn_size, but it's corrupted?
    memcpy(&us->regs[0], pes->regs, 3 * sizeof(uint64_t)); // TODO hardcoded reg count?
    us->max_stack = MAX_STACK;
    if (-1 == unwindstate__unwind(us)) {
      Map* map = procfs_MapMatch(us->pid, us->eip);
      printf("There was a bad error during unwinding %s (0x%lx).\n", map->path, us->eip);
      return;
    }
    FunLoc *locs = us->locs;
    for (uint64_t i = 0, j = 0; i < us->idx; i++) {
      uint64_t id_map, id_fun, id_loc;
      id_map = pprof_mapAdd(dp, locs[i].map_start, locs[i].map_end, locs[i].map_off, locs[i].sopath, "");
      id_fun = pprof_funAdd(dp, locs[i].funname, locs[i].funname, locs[i].srcpath, locs[i].line);
      id_loc = pprof_locAdd(dp, id_map, locs[i].ip, (uint64_t[]){id_fun}, (int64_t[]){0}, 1);
      if (id_loc > 0)
        id_locs[j++] = id_loc;
    }
    int64_t sample_val[max_watchers] = {0};
    sample_val[pos] = pes->period;
    pprof_sampleAdd(dp, sample_val, pctx->num_watchers, id_locs, us->idx);
    break;

  default:
    break;
  }

  // Click the timer at the end of processing, since we always add the sampling
  // rate to the last time.
  int64_t now = now_nanos();
  if (now > pctx->send_nanos) {
    int ret = 0;
    if ((ret = DDR_pprof(ddr, dp)))
      printf("Got an error adding pprof (%s)\n", DDR_code2str(ret));
    DDR_setTimeNano(ddr, dp->pprof.time_nanos, now);
    if ((ret = DDR_finalize(ddr)))
      printf("Got an error finalizing (%s)\n", DDR_code2str(ret));
    if ((ret = DDR_send(ddr)))
      printf("Got an error sending (%s)\n", DDR_code2str(ret));
    if ((ret = DDR_watch(ddr, -1)))
      printf("Got an error watching (%s)\n", DDR_code2str(ret));
    DDR_clear(ddr);
    pctx->send_nanos += pctx->params.upload_period*1000000000;

    // Prepare pprof for next window
    pprof_timeUpdate(dp);
  }
}

void print_help() {
  char help_msg[] = ""
                    " usage: dd-prof [--help] [PROFILER_OPTIONS] "
                    "COMMAND [COMMAND_ARGS]\n"
                    "\n"
                    "  -A, --apikey:\n"
                    "  -E, --environment:\n"
                    "  -H, --agent_host:\n"
                    "  -I, --agent_site:\n"
                    "  -N, --hostname:\n"
                    "  -P, --agent_port:\n"
                    "  -S, --service:\n"
                    "  -T, --tags:\n"
                    "  -U, --upload_timeout:\n"
                    "  -u, --upload_period:\n"
                    "  -e, --event:\n"
                    "  -v, --version:\n"
                    "  -x, --prefix:\n";
  printf("%s\n", help_msg);
}

int main(int argc, char **argv) {
  //---- Autodetect binary name
  char filename[128] = {0};
  char *fp           = strrchr("/"__FILE__, '/') + 1;
  memcpy(filename, fp, strlen(fp));
  (strrchr(filename, '.'))[0] = 0;

  //---- Inititiate structs
  int c = 0, oi = 0;
  struct DDProfContext *ctx =
      &(struct DDProfContext){.ddr        = &(DDReq){ .user_agent = "Native-http-client/0.1",
                                                     .language = "native",
                                                     .family = "native"},
                              .dp         = &(DProf){0},
                              .us         = &(struct UnwindState){0}};
  DDReq *ddr = ctx->ddr;
  DDR_init(ddr);

  struct option lopts[] = { OPT_TABLE(X_LOPT)
                           {"event", 1, 0, 'e'},
                           {"help", 0, 0, 'h'},
                           {"version", 0, 0, 'v'}};

  //---- Populate default values
  OPT_TABLE(X_DFLT);
  ctx->num_watchers = 2;
  ctx->watchers[0].opt = &perfoptions[10];
  ctx->watchers[0].sample_period = 9999999;   // Once per millisecond

  ctx->watchers[1].opt = &perfoptions[12];
  ctx->watchers[1].sample_period = 9999999;
  ctx->watchers[1].sample_period = 1;

  //---- Process Options
  if (argc <= 1) {
    print_help();
    return 0;
  }
  while (-1 != (c = getopt_long(argc, argv, "+" OPT_TABLE(X_OSTR) "e:h", lopts, &oi))) {
    switch (c) {
      OPT_TABLE(X_CASE)
    case 'e':
    case 'h':
      print_help();
      return 0;
    case 'v':
      return 0;
    default:
      printf("Non-recoverable error processing options.\n");
      return -1;
    }
  }

  // Replace string-type args
  ctx->params.enabled = true;
  ctx->params.upload_period = 60.0;


  // process upload_period
  if (ctx->upload_period) {
    double x = strtod(ctx->upload_period, NULL);
    if (x > 0.0)
      ctx->params.upload_period = x;
  }

#ifdef DD_DBG_PRINTARGS
  printf("=== PRINTING PARAMETERS ===\n");
  OPT_TABLE(X_PRNT);
  printf("upload_period: %f\n", ctx->params.upload_period);
#endif
  // Adjust input parameters for execvp()
  argv += optind;
  argc -= optind;

  if (argc <= 0) {
    printf("No program specified, exiting.\n");
    return -1;
  }

  /****************************************************************************\
  |                             Run the Profiler                               |
  \****************************************************************************/
  // Initialize the pprof
  char *pprof_labels[max_watchers];
  char *pprof_units[max_watchers];
  for (int i=0; i<ctx->num_watchers; i++) {
    pprof_labels[i] = ctx->watchers[i].opt->label;
    pprof_units[i] = ctx->watchers[i].opt->unit;
  }

  pprof_Init(ctx->dp, (const char**)pprof_labels, (const char**)pprof_units, ctx->num_watchers);
  pprof_timeUpdate(ctx->dp); // Set the time

  // Get the number of CPUs
  num_cpu = get_nprocs();

  // Setup a shared barrier for coordination
  pthread_barrierattr_t bat = {0};
  pthread_barrier_t *pb = mmap(NULL, sizeof(pthread_barrier_t),
      PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (MAP_FAILED == pb) {
    // TODO log here.  Nothing else to do, since we're not halting the target
    ctx->params.enabled = false;
  } else {
    pthread_barrierattr_init(&bat);
    pthread_barrierattr_setpshared(&bat, 1);
    pthread_barrier_init(pb, &bat, 2);
  }

  // Instrument the profiler
  // 1.   Setup pipes
  // 2.   fork()
  // 3p.  I am the original process.  If not prof profiling, instrument now
  // 3c.  I am the child.  Fork again and die.
  // 4p.  If not instrumenting profiler, instrument now.
  // 4cc. I am the grandchild.  I will profile.  Sit and listen for an FD
  // 5p.  Send the instrumentation FD.  Repeat for each instrumentation point.
  // 5cc. Receive.  Repeat.  This is known before time of fork.
  // 6p.  close fd, teardown pipe, execvp() to target process.
  // 6cc. teardown pipe, create mmap regions and enter event loop

  // TODO
  // * Multiple file descriptors can be sent in one push.  I don't know how much
  //   this matters, but if every microsecond counts at startup, it's an option.

  // 1. Setup pipes (really unix domain socket pair)
  int sfd[2] = {-1, -1};
  if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sfd)) {
    // TODO log here
    ctx->params.enabled = false;
  }

  // 2. fork()
  pid_t pid = fork();
  if (!pid) {
    // 3c. I am the child.  Fork again
    if (fork()) exit(0); // no need to use _exit, since we still own the proc

    // 4cc. I am the grandchild.  I will profile.  Sit and listen for an FD
    bool startup_errors = false;
    struct PEvent pes[100] = {0};
    for (int i = 0; i < ctx->num_watchers; i++) {
      for (int j = 0; j < num_cpu; j++) {
        int k = i*num_cpu + j;
        pes[k].pos = i;  // watcher index is the sample index
        pes[k].fd = getfd(sfd[0]);
        if (!(pes[k].region = perfown(pes[k].fd))) {
          startup_errors = true;
        }
        pthread_barrier_wait(pb); // Tell the other guy I have his FD
      }
    }

    // Cleanup and enter event loop
    close(sfd[0]);
    close(sfd[1]);
    munmap(pb, sizeof(pthread_barrier_t));

    ctx->send_nanos = now_nanos() + ctx->params.upload_period*1000000000;
    elf_version(EV_CURRENT); // Initialize libelf
    if (!startup_errors)
      main_loop(pes, ctx->num_watchers*num_cpu, ddprof_callback, ctx);
    else
      printf("Started with errors\n");
  } else {
    // 3p.  I am the original process.  If not prof profiling, instrument now
    // TODO: Deadlock city!  Add some timeouts here for chrissake
    pid_t mypid = getpid();
    for (int i = 0; i < ctx->num_watchers && ctx->params.enabled; i++) {
      for(int j=0; j<num_cpu; j++) {
        int fd = perfopen(mypid, ctx->watchers[i].opt->type, ctx->watchers[i].opt->config, ctx->watchers[i].sample_period, j);
        if (-1 == fd || sendfd(sfd[1], fd)) {
          // TODO this is an error, so log it later
          printf("Had an error.\n");
          ctx->params.enabled = false;
        }
        pthread_barrier_wait(pb); // Did the profiler get the FD?
        close(fd);
      }
    }

    // Cleanup and become desired process image
    close(sfd[0]);
    close(sfd[1]);
    munmap(pb, sizeof(pthread_barrier_t));

    execvp(argv[0], argv);
    printf("Hey, this shouldn't happen!\n");
  }

  // Neither the profiler nor the instrumented process should get here
  return -1;
}
