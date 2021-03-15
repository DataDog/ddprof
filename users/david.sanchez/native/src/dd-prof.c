#include <getopt.h>
#include <libelf.h>
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "dd_send.h"
#include "http.h"
#include "perf.h"
#include "pprof.h"
#include "unwind.h"

struct DDProfContext {
  DProf *dp;
  DDReq *ddr;

  // Parameters for interpretation
  char *enabled;
  char *agent_host;
  char *prefix;
  char *tags;
  char *upload_timeout;
  char *sample_rate;
  char *upload_period;
  struct {
    char enabled;
    uint32_t upload_timeout;
    uint32_t sample_rate;
    uint32_t upload_period;
  } params;

  struct UnwindState *us;
  double sample_sec;
  int64_t send_nanos;
};

// clang-format off
/*
    This table is used for a variety of things, but primarily for dispatching
    input in a consistent way across the application.  Values may come from one
    of several places, with defaulting in the following order:
      1. Commandline argument
      2. Configuration file
      3. Environment variable
      4. Application default

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
  X(DD_AGENT_HOST,               agent_host,      H, 1, ctx,      NULL, "localhost") \
  X(DD_SITE,                     site,            I, 1, ctx->ddr, NULL, NULL)        \
  X(DD_HOST_OVERRIDE,            host,            N, 1, ctx->ddr, NULL, "localhost") \
  X(DD_TRACE_AGENT_PORT,         port,            P, 1, ctx->ddr, NULL, "8081")      \
  X(DD_SERVICE,                  service,         S, 1, ctx->ddr, NULL, "my_profiled_service") \
  X(DD_TAGS,                     tags,            T, 1, ctx,      NULL, NULL)        \
  X(DD_PROFILING_UPLOAD_TIMEOUT, upload_timeout,  U, 1, ctx,      NULL, "10")        \
  X(DD_VERSION,                  profiler_version,V, 1, ctx->ddr, NULL, NULL)        \
  X(DD_PROFILING_ENABLED,        enabled,         e, 1, ctx,      NULL, "yes")       \
  X(DD_PROFILING_NATIVE_RATE,    sample_rate,     r, 1, ctx,      NULL, "1000")      \
  X(DD_PROFILING_UPLOAD_PERIOD,  upload_period,   u, 1, ctx,      NULL, "60")        \
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

void ddprof_callback(struct perf_event_header *hdr, void *arg) {
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
    memcpy(&us->regs[0], pes->regs, 3 * sizeof(uint64_t));
    us->max_stack = MAX_STACK;
    if (-1 == unwindstate__unwind(us)) {
      printf("There was a bad error during unwinding (0x%lx).\n", us->eip);
      return;
    }
    FunLoc *locs = us->locs;
    for (uint64_t i = 0, j = 0; i < us->idx; i++, j++) {
      uint64_t id_map, id_fun, id_loc;
      id_map = pprof_mapAdd(dp, locs[i].map_start, locs[i].map_end, locs[i].map_off, locs[i].sopath, "");
      id_fun = pprof_funAdd(dp, locs[i].funname, locs[i].funname, locs[i].srcpath, locs[i].line);
      id_loc = pprof_locAdd(dp, id_map, locs[i].ip, (uint64_t[]){id_fun}, (int64_t[]){0}, 1);
      if (id_loc > 0)
        id_locs[j] = id_loc;
      else
        j--;

//      printf("   %s\n", locs[i].funname);
    }
    int64_t this_time = now_nanos();
    pprof_sampleAdd(dp, (int64_t[]){1, pes->period, this_time-last_time}, 3, id_locs, us->idx);
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
      printf("Got an error (%s)\n", DDR_code2str(ret));
    DDR_setTimeNano(ddr, dp->pprof.time_nanos, now);
    if ((ret = DDR_finalize(ddr)))
      printf("Got an error (%s)\n", DDR_code2str(ret));
    if ((ret = DDR_send(ddr)))
      printf("Got an error (%s)\n", DDR_code2str(ret));
    if ((ret = DDR_watch(ddr, -1)))
      printf("Got an error (%s)\n", DDR_code2str(ret));
    DDR_clear(ddr);
    pctx->send_nanos += pctx->sample_sec*1000000000;

    // Prepare pprof for next window
    pprof_timeUpdate(dp);
  }
}

void print_help() {
  char help_msg[] = ""
                    " usage: dd-prof [--version] [--help] [PROFILER_OPTIONS] "
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
                    "  -V, --version:\n"
                    "  -e, --enabled:\n"
                    "  -r, --sample_rate:\n"
                    "  -u, --upload_period:\n"
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
      &(struct DDProfContext){.ddr        = &(DDReq){.apikey = "1c77adb933471605ccbe82e82a1cf5cf",
                                                     .host = "host.docker.internal",
                                                     .port = "10534",
                                                     .user_agent = "Native-http-client/0.1",
                                                     .language = "native",
                                                     .family = "native"},
//                                                     .runtime = "native"},
                              .dp         = &(DProf){0},
                              .us         = &(struct UnwindState){0},
                              .sample_sec = 60.0};
  DDReq *ddr = ctx->ddr;
  DDR_init(ddr);

  struct option lopts[] = {OPT_TABLE(X_LOPT){"help", 0, 0, 'h'}};

  //---- Populate default values
  OPT_TABLE(X_DFLT);

  //---- Process Options
  while (-1 != (c = getopt_long(argc, argv, "+" OPT_TABLE(X_OSTR) "h", lopts, &oi))) {
    switch (c) {
      OPT_TABLE(X_CASE)
    case 'h':
      print_help();
      return 0;
    default:
      printf("Non-recoverable error processing options.\n");
      exit(-1);
    }
  }

#ifdef DD_DBG_PRINTARGS
  printf("=== PRINTING PARAMETERS ===\n");
  OPT_TABLE(X_PRNT);
#endif
  // Adjust input parameters for execvp()
  argv += optind;
  argc -= optind;

  if (argc <= 0) {
    printf("No program specified, exiting.\n");
    exit(-1);
  }

  /****************************************************************************\
  |                             Run the Profiler                               |
  \****************************************************************************/
  // Initialize the pprof
  pprof_Init(ctx->dp, (const char **)&(const char *[]){"samples", "cpu-time", "wall-time"},
             (const char **)&(const char *[]){"count", "nanoseconds", "nanoseconds"}, 3);
  pprof_timeUpdate(ctx->dp); // Set the time

  // Set the CPU affinity so that everything is on the same CPU.  Scream about
  // it because we want to undo this later..!
  cpu_set_t cpu_mask = {0};
  CPU_SET(0, &cpu_mask);
  if (!sched_setaffinity(getpid(), sizeof(cpu_set_t), &cpu_mask)) {
    printf("Successfully set the CPU mask.\n");
  } else {
    printf("Failed to set the CPU mask.\n");
    return -1;
  }

  // Setup a shared barrier for timing
  pthread_barrierattr_t bat = {0};
  pthread_barrier_t *pb =
      mmap(NULL, sizeof(pthread_barrier_t), PROT_READ | PROT_WRITE,
           MAP_SHARED | MAP_ANONYMOUS, -1, 0);

  pthread_barrierattr_init(&bat);
  pthread_barrierattr_setpshared(&bat, 1);
  pthread_barrier_init(pb, &bat, 2);

  // Fork, then run the child
  pid_t pid = fork();
  if (!pid) {
    pthread_barrier_wait(pb);
    munmap(pb, sizeof(pthread_barrier_t));
    execvp(argv[1], argv + 1);
    printf("Hey, this shouldn't happen!\n");
    return -1;
  } else {
    PEvent pe = {0};
    char err  = perfopen(pid, &pe, NULL);
    if (-1 == err) {
      printf("Couldn't set up perf_event_open\n");
      return -1;
    }
    pthread_barrier_wait(pb);

    // If we're here, the child just launched.  Start the timer
    ctx->send_nanos = now_nanos() + ctx->sample_sec*1000000000;
    munmap(pb, sizeof(pthread_barrier_t));
    elf_version(EV_CURRENT);
    main_loop(&pe, ddprof_callback, ctx);
  }

  return 0;
}
