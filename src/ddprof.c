#include <execinfo.h>
#include <getopt.h>
#include <libelf.h>
#include <pthread.h>
#include <signal.h>
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
#include "version.h"

#define max_watchers 10

typedef struct PerfOption {
  char *desc;
  char *key;
  int type;
  int config;
  int base_rate;
  char *label;
  char *unit;
  int mode;
} PerfOption;

struct DDProfContext {
  DProf *dp;
  DDReq *ddr;

  // Parameters for interpretation
  char *agent_host;
  char *prefix;
  char *tags;

  // Input parameters
  char *count_samples;
  char *enabled;
  char *upload_period;
  char *profprofiler;
  struct {
    bool count_samples;
    bool enabled;
    double upload_period;
    bool profprofiler;
  } params;
  struct watchers {
    PerfOption *opt;
    uint64_t sample_period;
  } watchers[max_watchers];
  int num_watchers;

  struct UnwindState *us;
  int64_t send_nanos;
};

// RE: kernel tracepoints
// I don't think there's any commitment that these IDs are unchanging between
// installations of the same kernel version, let alone between kernel releases.
// You'll have to scrape them at runtime.  This is actually nice because access
// to the sysfs endpoint precludes permissions, so you can emit a helpful error
// if something goes wrong before instrumenting with perf To generate the hack:
// for f in $(find /sys/kernel/tracing/events/block -name id); do echo $f; cat
// $f; done
// clang-format off
PerfOption perfoptions[] = {
  // Hardware
  {"CPU Cycles",      "hCPU",    PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES,              1e6, "cpu-cycle",      "cycles",       0},
  {"Ref. CPU Cycles", "hREF",    PERF_TYPE_HARDWARE, PERF_COUNT_HW_REF_CPU_CYCLES,          1e6, "ref-cycle",      "cycles",       0},
  {"Instr. Count",    "hINSTR",  PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS,            1e6, "cpu-instr",      "instructions", 0},
  {"Cache Ref.",      "hCREF",   PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES,        1e3, "cache-ref",      "events",       0},
  {"Cache Miss",      "hCMISS",  PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES,            1e3, "cache-miss",     "events",       0},
  {"Branche Instr.",  "hBRANCH", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS,     1e3, "branch-instr",   "events",       0},
  {"Branch Miss",     "hBMISS",  PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES,           1e3, "branch-miss",    "events",       0},
  {"Bus Cycles",      "hBUS",    PERF_TYPE_HARDWARE, PERF_COUNT_HW_BUS_CYCLES,              1e3, "bus-cycle",      "cycles",       0},
  {"Bus Stalls(F)",   "hBSTF",   PERF_TYPE_HARDWARE, PERF_COUNT_HW_STALLED_CYCLES_FRONTEND, 1e3, "bus-stf",        "cycles",       0},
  {"Bus Stalls(B)",   "hBSTB",   PERF_TYPE_HARDWARE, PERF_COUNT_HW_STALLED_CYCLES_BACKEND,  1e3, "bus-stb",        "cycles",       0},
  {"CPU Time",        "sCPU",    PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK,              1e6, "cpu-time",       "nanoseconds",  0},
  {"Wall? Time",      "sWALL",   PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_CLOCK,               1e6, "wall-time",      "nanoseconds",  0},
  {"Ctext Switches",  "sCI",     PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES,        1,   "switches",       "events",       PE_KERNEL_INCLUDE},
  {"Block-Insert",    "kBLKI",   PERF_TYPE_TRACEPOINT, 1133,                                1,   "block-insert",   "events",       PE_KERNEL_INCLUDE},
  {"Block-Issue",     "kBLKS",   PERF_TYPE_TRACEPOINT, 1132,                                1,   "block-issue",    "events",       PE_KERNEL_INCLUDE},
  {"Block-Complete",  "kBLKC",   PERF_TYPE_TRACEPOINT, 1134,                                1,   "block-complete", "events",       PE_KERNEL_INCLUDE},
};
// clang-format on

int num_perfs = sizeof(perfoptions) / sizeof(*perfoptions);
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
#define X_ENUM(a, b, c, d, e, f, g) a,
#define X_OSTR(a, b, c, d, e, f, g) #c ":"
#define X_DFLT(a, b, c, d, e, f, g) DFLT_EXP(#a, b, e, f, g);
#define X_CASE(a, b, c, d, e, f, g) case *#c: (e)->b = optarg; break;
#define X_PRNT(a, b, c, d, e, f, g) printf(#b ": %s\n", (e)->b);

//  A                            B                C  D  E         F     G
#define OPT_TABLE(XX)                                                                \
  XX(DD_API_KEY,                  apikey,          A, 1, ctx->ddr, NULL, NULL)        \
  XX(DD_ENV,                      environment,     E, 1, ctx->ddr, NULL, NULL)        \
  XX(DD_AGENT_HOST,               host,            H, 1, ctx->ddr, NULL, "localhost") \
  XX(DD_SITE,                     site,            I, 1, ctx->ddr, NULL, NULL)        \
  XX(DD_TRACE_AGENT_PORT,         port,            P, 1, ctx->ddr, NULL, "8081")      \
  XX(DD_SERVICE,                  service,         S, 1, ctx->ddr, NULL, "my_profiled_service") \
  XX(DD_TAGS,                     tags,            T, 1, ctx,      NULL, NULL)        \
  XX(DD_VERSION,                  profiler_version,V, 1, ctx->ddr, NULL, NULL)        \
  XX(DD_PROFILING_ENABLED,        enabled,         d, 1, ctx,      NULL, "yes")       \
  XX(DD_PROFILING_COUNTSAMPLES,   count_samples,   c, 1, ctx,      NULL, "yes")       \
  XX(DD_PROFILING_UPLOAD_PERIOD,  upload_period,   u, 1, ctx,      NULL, "60.0")      \
  XX(DD_PROFILE_NATIVEPROFILER,   profprofiler,    p, 0, ctx,      NULL, NULL)        \
  XX(DD_PROFILING_,               prefix,          X, 1, ctx,      NULL, "")
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

typedef enum DDKeys {
  OPT_TABLE(X_ENUM)
  DD_KLEN
} DDKeys;



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
    us->idx      = 0;         // Modified during unwinding; has stack depth
    us->stack    = pes->data;
    us->stack_sz = pes->size; // TODO should be dyn_size, but it's corrupted?
    memcpy(&us->regs[0], pes->regs,  3 * sizeof(uint64_t)); // TODO hardcoded reg count?
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


/*********************************  Printers  *********************************/
#define X_HLPK(a, b, c, d, e, f, g) "  -"#c", --"#b", (envvar: "#a")",

// We use a non-NULL definition for an undefined string, because it's important
// that this table is always populated intentionally.  This is checked in the
// loop inside print_help()
#define STR_UNDF (char*)1
char* help_str[DD_KLEN] = {
  [DD_API_KEY] =
"    A valid Datadog API key.  Passing the API key will cause "MYNAME" to bypass\n"
"    the Datadog agent.  Erroneously adding this key might break an otherwise\n"
"    functioning deployment!\n",
  [DD_ENV] =
"    The name of the environment to use in the Datadog UI.\n",
  [DD_AGENT_HOST] =
"    The hostname to use for intake.  This is either the hostname for the agent\n"
"    or the backend endpoint, if bypassing the agent.\n",
  [DD_SITE] = STR_UNDF,
  [DD_TRACE_AGENT_PORT] =
"    The intake port for the Datadog agent or backend system.\n",
  [DD_SERVICE] =
"    The name of this service\n",
  [DD_TAGS] = STR_UNDF,
  [DD_VERSION] = STR_UNDF,
  [DD_PROFILING_ENABLED] = STR_UNDF,
  [DD_PROFILING_COUNTSAMPLES] = STR_UNDF,
  [DD_PROFILING_UPLOAD_PERIOD] =
"    In seconds, how frequently to upload gathered data to Datadog.\n"
"    Currently, it is recommended to keep this value to 60 seconds, which is\n"
"    also the default.\n",
  [DD_PROFILE_NATIVEPROFILER] = STR_UNDF,
  [DD_PROFILING_] = STR_UNDF,
};

char* help_key[DD_KLEN] = {
  OPT_TABLE(X_HLPK)
};

void print_version() {
  printf(MYNAME" %d.%d.%d", VER_MAJ, VER_MIN, VER_PATCH);
  if (*VER_REV)
    printf("+%s", VER_REV);
  printf("\n");
}

void print_help() {
  char help_hdr[] = ""
" usage: "MYNAME" [--help] [PROFILER_OPTIONS] COMMAND [COMMAND_ARGS]\n"
" eg: "MYNAME" -A hunter2 -H localhost -P 8192 redis-server /etc/redis/redis.conf\n\n";

  char help_opts_extra[] =
"  -e, --event:\n"
"    A string representing the events to sample.  Defaults to `cw`\n"
"    See the `events` section below for more details.\n"
"    eg: --event sCPU --event hREF\n"
"  -v, --version:\n"
"    Prints the version of "MYNAME" and exits.\n";

  char help_events[] =
"Events\n"
MYNAME" can register to various system events in order to customize the\n"
"information retrieved during profiling.  Note that certain events can add\n"
"more overhead during profiling; be sure to test your service under a realistic\n"
"load simulation to ensure the desired forms of profiling are acceptable.\n"
"\n"
"The listing below gives the string to pass to the --event argument, a\n"
"brief description of the event, the name of the event as it will appear in\n"
"the Datadog UI, and the units.\n"
"Events with the same name in the UI conflict with each other; be sure to pick\n"
"only one such event!\n"
"\n";

  printf("%s", help_hdr);
  printf("Options:\n");
  for (int i=0; i<DD_KLEN; i++) {
    assert(help_str[i]);
    if (help_str[i] && STR_UNDF != help_str[i]) {
      printf("%s\n", help_key[i]);
      printf("%s\n", help_str[i]);
    }
  }
  printf("%s\n", help_opts_extra);
  printf("%s", help_events);
  for (int i = 0; i < num_perfs; i++)
    printf("%-10s - %-15s (%s, %s)\n", perfoptions[i].key, perfoptions[i].desc, perfoptions[i].label, perfoptions[i].unit);
}


/*****************************  SIGSEGV Handler *******************************/
void sigsegv_handler(int sig) {
  // TODO should this print the version
(void)sig;
  static void *buf[16] = {0};
  static char errmsg[] = MYNAME" has encountered an unrecoverable error and will now exit.\n";
  size_t sz = backtrace(buf, 16);
  write(STDERR_FILENO, errmsg, strlen(errmsg));
  backtrace_symbols_fd(buf, sz, STDERR_FILENO);
  exit(-1);
}


/******************************  Entrypoint  **********************************/
int main(int argc, char **argv) {
  //---- Autodetect binary name
  char filename[128] = {0};
  char *fp           = strrchr("/"__FILE__, '/') + 1;
  memcpy(filename, fp, strlen(fp));
  (strrchr(filename, '.'))[0] = 0;

  //---- Inititiate structs
  int c = 0, oi = 0;
  struct DDProfContext *ctx =
      &(struct DDProfContext){.ddr  = &(DDReq){.user_agent = "Native-http-client/0.1",
                                               .language = "native",
                                               .family = "native"},
                              .dp   = &(DProf){0},
                              .us   = &(struct UnwindState){0}};
  DDReq *ddr = ctx->ddr;
  DDR_init(ddr);

  struct option lopts[] = { OPT_TABLE(X_LOPT)
                           {"event",   1, 0, 'e'},
                           {"help",    0, 0, 'h'},
                           {"version", 0, 0, 'v'}};

  //---- Populate default values
  OPT_TABLE(X_DFLT);
  bool default_watchers = true;
  ctx->num_watchers = 1;
  ctx->watchers[0].opt = &perfoptions[10];
  ctx->watchers[0].sample_period = perfoptions[10].base_rate;

  //---- Process Options
  if (argc <= 1) {
    print_help();
    return 0;
  }
  while (-1 != (c = getopt_long(argc, argv, "+" OPT_TABLE(X_OSTR) "e:hv", lopts, &oi))) {
    switch (c) {
      OPT_TABLE(X_CASE)
    case 'e':;
      bool event_matched = false;
      for (int i = 0; i < num_perfs; i++) {
        if (!strncmp(perfoptions[i].key, optarg, strlen(perfoptions[i].desc))) {

          // If we got a match, then we need to use non-default accounting
          if (default_watchers) {
            default_watchers = false;
            ctx->num_watchers = 0;
          }

          ctx->watchers[ctx->num_watchers].opt = &perfoptions[i];

          // TODO, here check for a comma to determine the sampling rate
          ctx->watchers[ctx->num_watchers].sample_period = perfoptions[i].base_rate;
          ctx->num_watchers++;

          // Early exit
          event_matched = true;
          break;
        }
      }
      if (!event_matched) {
        printf("Event %s did not match any events.\n", optarg);
      }
      break;
    case 'h':
      print_help();
      return 0;
    case 'v':
      print_version();
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

  // process count_samples
  if (ctx->count_samples) {
    if (!strcmp(ctx->count_samples, "yes") || strcmp(ctx->count_samples, "true"))
      ctx->params.count_samples = true;
  }

#ifdef DD_DBG_PRINTARGS
  printf("=== PRINTING PARAMETERS ===\n");
  OPT_TABLE(X_PRNT);
  printf("upload_period: %f\n", ctx->params.upload_period);

  printf("Instrumented with %d watchers.\n", ctx->num_watchers);
  for (int i=0; i < ctx->num_watchers; i++) {
    printf("ID: %s, Pos: %d, Index: %d, Label: %s, Mode: %d\n",
        ctx->watchers[i].opt->key,
        i,
        ctx->watchers[i].opt->config,
        ctx->watchers[i].opt->label,
        ctx->watchers[i].opt->mode);
  }
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

  if(!pprof_Init(ctx->dp, (const char**)pprof_labels, (const char**)pprof_units, ctx->num_watchers)) {
    printf("Failed to initialize pprof\n");
    return -1;
  }
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
      signal(SIGSEGV, sigsegv_handler);
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
          int fd = perfopen(mypid, ctx->watchers[i].opt->type, ctx->watchers[i].opt->config, ctx->watchers[i].sample_period, ctx->watchers[i].opt->mode, j);
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

// Cleanup after yourself
#undef STR_UNDF
