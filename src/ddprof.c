#include <ddprof/dd_send.h>
#include <ddprof/http.h>
#include <ddprof/pprof.h>
#include <execinfo.h>
#include <getopt.h>
#include <libelf.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <wait.h>

#include "ddprofcmdline.h"
#include "ipc.h"
#include "logger.h"
#include "perf.h"
#include "statsd.h"
#include "unwind.h"
#include "version.h"

#define max_watchers 10

typedef struct DDProfContext {
  DProf *dp;
  DDReq *ddr;

  // Parameters for interpretation
  char *agent_host;
  char *prefix;
  char *tags;
  char *logmode;
  char *loglevel;

  // Input parameters
  char *printargs;
  char *count_samples;
  char *enable;
  char *native_enable;
  char *upload_period;
  char *profprofiler;
  char *faultinfo;
  char *sendfinal;
  char *pid;
  char *global;
  struct {
    bool count_samples;
    bool enable;
    double upload_period;
    bool profprofiler;
    bool faultinfo;
    bool sendfinal;
    pid_t pid;
    bool global;
  } params;
  PerfOption watchers[max_watchers];
  int num_watchers;

  struct UnwindState *us;
  int64_t send_nanos; // Last time an export was sent
} DDProfContext;

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
  {"CPU Cycles",      "hCPU",    PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES,              1e2, "cpu-cycle",      "cycles", .freq = true},
  {"Ref. CPU Cycles", "hREF",    PERF_TYPE_HARDWARE, PERF_COUNT_HW_REF_CPU_CYCLES,          1e3, "ref-cycle",      "cycles", .freq = true},
  {"Instr. Count",    "hINSTR",  PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS,            1e3, "cpu-instr",      "instructions", .freq = true},
  {"Cache Ref.",      "hCREF",   PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES,        1e3, "cache-ref",      "events"},
  {"Cache Miss",      "hCMISS",  PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES,            1e3, "cache-miss",     "events"},
  {"Branche Instr.",  "hBRANCH", PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS,     1e3, "branch-instr",   "events"},
  {"Branch Miss",     "hBMISS",  PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES,           1e3, "branch-miss",    "events"},
  {"Bus Cycles",      "hBUS",    PERF_TYPE_HARDWARE, PERF_COUNT_HW_BUS_CYCLES,              1e3, "bus-cycle",      "cycles", .freq = true},
  {"Bus Stalls(F)",   "hBSTF",   PERF_TYPE_HARDWARE, PERF_COUNT_HW_STALLED_CYCLES_FRONTEND, 1e3, "bus-stf",        "cycles", .freq = true},
  {"Bus Stalls(B)",   "hBSTB",   PERF_TYPE_HARDWARE, PERF_COUNT_HW_STALLED_CYCLES_BACKEND,  1e3, "bus-stb",        "cycles", .freq = true},
  {"CPU Time",        "sCPU",    PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK,              1e2, "cpu-time",       "nanoseconds", . freq = true},
  {"Wall? Time",      "sWALL",   PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_CLOCK,               1e2, "wall-time",      "nanoseconds", . freq = true},
  {"Ctext Switches",  "sCI",     PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES,        1,   "switches",       "events", .include_kernel = true},
  {"Block-Insert",    "kBLKI",   PERF_TYPE_TRACEPOINT, 1133,                                1,   "block-insert",   "events", .include_kernel = true},
  {"Block-Issue",     "kBLKS",   PERF_TYPE_TRACEPOINT, 1132,                                1,   "block-issue",    "events", .include_kernel = true},
  {"Block-Complete",  "kBLKC",   PERF_TYPE_TRACEPOINT, 1134,                                1,   "block-complete", "events", .include_kernel = true},
  {"Malloc",          "bMalloc", PERF_TYPE_BREAKPOINT, 0,                                   1,   "malloc",         "events", .bp_type = HW_BREAKPOINT_X},
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
  D - Short option character
  E - Long option
  F - Destination struct (WOW, THIS IS HORRIBLE)
  G - defaulting function (or NULL)
  H - Fallback value, if any
*/
#define X_LOPT(a, b, c, d, e, f, g, h) {#b, e, 0, d},
#define X_ENUM(a, b, c, d, e, f, g, h) a,
#define X_OSTR(a, b, c, d, e, f, g, h) #c ":"
#define X_DFLT(a, b, c, d, e, f, g, h) DFLT_EXP(#a, b, f, g, h);
#define X_FREE(a, b, c, d, e, f, g, h) FREE_EXP(b, f);
#define X_CASE(a, b, c, d, e, f, g, h) CASE_EXP(d, f, b)
#define X_PRNT(a, b, c, d, e, f, g, h) if((f)->b) LG_NTC("  "#b ": %s", (f)->b);

//  A                              B                C   D   E  F         G     H
#define OPT_TABLE(XX)                                                                       \
  XX(DD_API_KEY,                   apikey,          A, 'A', 1, ctx->ddr, NULL, "")          \
  XX(DD_ENV,                       environment,     E, 'E', 1, ctx->ddr, NULL, "")          \
  XX(DD_AGENT_HOST,                host,            H, 'H', 1, ctx->ddr, NULL, "localhost") \
  XX(DD_SITE,                      site,            I, 'I', 1, ctx->ddr, NULL, "")          \
  XX(DD_TRACE_AGENT_PORT,          port,            P, 'P', 1, ctx->ddr, NULL, "80")        \
  XX(DD_SERVICE,                   service,         S, 'S', 1, ctx->ddr, NULL, "myservice") \
  XX(DD_TAGS,                      tags,            T, 'T', 1, ctx,      NULL, "")          \
  XX(DD_VERSION,                   serviceversion,  V, 'V', 1, ctx->ddr, NULL, "")          \
  XX(DD_PROFILING_ENABLED,         enable,          d, 'd', 1, ctx,      NULL, "yes")       \
  XX(DD_PROFILING_NATIVE_ENABLED,  native_enable,   n, 'n', 1, ctx,      NULL, "yes")       \
  XX(DD_PROFILING_COUNTSAMPLES,    count_samples,   c, 'c', 1, ctx,      NULL, "yes")       \
  XX(DD_PROFILING_UPLOAD_PERIOD,   upload_period,   u, 'u', 1, ctx,      NULL, "60")        \
  XX(DD_PROFILE_NATIVEPROFILER,    profprofiler,    r, 'r', 0, ctx,      NULL, "")          \
  XX(DD_PROFILING_,                prefix,          X, 'X', 1, ctx,      NULL, "")          \
  XX(DD_PROFILING_NATIVEFAULTINFO, faultinfo,       s, 's', 1, ctx,      NULL, "yes")       \
  XX(DD_PROFILING_NATIVEPRINTARGS, printargs,       a, 'a', 1, ctx,      NULL, "no")        \
  XX(DD_PROFILING_NATIVESENDFINAL, sendfinal,       f, 'f', 1, ctx,      NULL, "")          \
  XX(DD_PROFILING_NATIVELOGMODE,   logmode,         o, 'o', 1, ctx,      NULL, "stdout")    \
  XX(DD_PROFILING_NATIVELOGLEVEL,  loglevel,        l, 'l', 1, ctx,      NULL, "warn")      \
  XX(DD_PROFILING_NATIVETARGET,    pid,             p, 'p', 1, ctx,      NULL, "")          \
  XX(DD_PROFILING_NATIVEGLOBAL,    global,          g, 'g', 1, ctx,      NULL, "")
// clang-format on

// TODO das210603 I don't think this needs to be inlined as a macro anymore
#define DFLT_EXP(evar, key, targ, func, dfault)                                \
  __extension__({                                                              \
    char *_buf = NULL;                                                         \
    if (!((targ)->key)) {                                                      \
      if (evar && getenv(evar))                                                \
        _buf = strdup(getenv(evar));                                           \
      else if (*dfault)                                                        \
        _buf = strdup(dfault);                                                 \
      (targ)->key = _buf;                                                      \
    }                                                                          \
  })

#define CASE_EXP(casechar, targ, key)                                          \
  case casechar:                                                               \
    if ((targ)->key)                                                           \
      free((targ)->key);                                                       \
    (targ)->key = strdup(optarg);                                              \
    break;

// TODO das210603 I don't think this needs to be inlined as a macro anymore
#define FREE_EXP(key, targ)                                                    \
  __extension__({                                                              \
    if ((targ)->key)                                                           \
      free((targ)->key);                                                       \
    (targ)->key = NULL;                                                        \
  })

typedef enum DDKeys { OPT_TABLE(X_ENUM) DD_KLEN } DDKeys;

// This is the socket used to report things over statsd, if available
int fd_statsd = -1;

void statsd_init() {
  char *path_statsd = NULL;
  if ((path_statsd = getenv("DD_DOGSTATSD_SOCKET")))
    fd_statsd = statsd_open(path_statsd, strlen(path_statsd));
}

struct BrittleStringTable {
  unsigned char *regions[32];
  unsigned char *arena;
  size_t capacity;
};

void statsd_upload_globals(DDProfContext *ctx) {
  static char key_rss[] = "datadog.profiler.native.rss";
  static char key_user[] = "datadog.profiler.native.utime";
  static char key_st_elements[] = "datadog.profiler.native.pprof.st_elements";
  if (-1 == fd_statsd)
    return;

  // Upload some procfs values
  static unsigned long last_utime = 0;
  ProcStatus *procstat = proc_read();
  if (procstat) {
    statsd_send(fd_statsd, key_rss, &(long){1024 * procstat->rss}, STAT_GAUGE);
    if (procstat->utime) {
      long this_time = procstat->utime - last_utime;
      statsd_send(fd_statsd, key_user, &(long){this_time}, STAT_GAUGE);
      last_utime = procstat->utime;
    }
  }

  // Upload some internal stats
  uint64_t st_size = ctx->dp->string_table_size(ctx->dp->string_table_data);
  statsd_send(fd_statsd, key_st_elements, &(long){st_size}, STAT_GAUGE);
}

/******************************  Perf Callback  *******************************/
char *pprof_labels[max_watchers];
char *pprof_units[max_watchers];
static inline int64_t now_nanos() {
  static struct timeval tv = {0};
  gettimeofday(&tv, NULL);
  return (tv.tv_sec * 1000000 + tv.tv_usec) * 1000;
}

void export(DDProfContext *pctx, int64_t now) {
  DDReq *ddr = pctx->ddr;
  DProf *dp = pctx->dp;

  LG_NTC("Pushed samples to backend");
  int ret = 0;
  if ((ret = DDR_pprof(ddr, dp)))
    LG_ERR("Error enqueuing pprof (%s)", DDR_code2str(ret));
  DDR_setTimeNano(ddr, dp->pprof.time_nanos, now);
  if ((ret = DDR_finalize(ddr)))
    LG_ERR("Error finalizing export (%s)", DDR_code2str(ret));
  if ((ret = DDR_send(ddr)))
    LG_ERR("Error sending export (%s)", DDR_code2str(ret));
  if ((ret = DDR_watch(ddr, -1)))
    LG_ERR("Error(%d) watching (%s)", ddr->res.code, DDR_code2str(ret));
  DDR_clear(ddr);

  // Update the time last sent
  pctx->send_nanos += pctx->params.upload_period * 1000000000;

  // Prepare pprof for next window
  pprof_timeUpdate(dp);
}

void ddprof_timeout(void *arg) {
  DDProfContext *pctx = arg;
  int64_t now = now_nanos();

  if (now > pctx->send_nanos)
    export(pctx, now);
}

void ddprof_callback(struct perf_event_header *hdr, int pos, void *arg) {
  static uint64_t id_locs[MAX_STACK] = {0};

  DDProfContext *pctx = arg;
  struct UnwindState *us = pctx->us;
  DProf *dp = pctx->dp;
  struct perf_event_sample *pes;

  switch (hdr->type) {
  case PERF_RECORD_SAMPLE:
    pes = (struct perf_event_sample *)hdr;
    us->pid = pes->pid;
    us->idx = 0; // Modified during unwinding; has stack depth
    us->stack = pes->data;
    us->stack_sz = pes->size; // TODO should be dyn_size, but it's corrupted?
    memcpy(&us->regs[0], pes->regs, 3 * sizeof(uint64_t));
    us->max_stack = MAX_STACK;
    FunLoc_clear(us->locs);
    if (-1 == unwindstate__unwind(us)) {
      Dso *dso = dso_find(us->pid, us->eip);
      if (!dso) {
        LG_WRN("Error getting map for [%d](0x%lx)", us->pid, us->eip);
        analyze_unwinding_error(us->pid, us->eip);
      } else {
        LG_WRN("Error unwinding %s [%d](0x%lx)", dso_path(dso), us->pid,
               us->eip);
      }
      return;
    }
    FunLoc *locs = us->locs;
    for (uint64_t i = 0, j = 0; i < us->idx; i++) {
      FunLoc L = locs[i];
      uint64_t id_map, id_fun, id_loc;

      // Using the sopath instead of srcpath in locAdd for the DD UI
      id_map =
          pprof_mapAdd(dp, L.map_start, L.map_end, L.map_off, L.sopath, "");
      id_fun = pprof_funAdd(dp, L.funname, L.funname, L.srcpath, 0);
      id_loc = pprof_locAdd(dp, id_map, 0, (uint64_t[]){id_fun},
                            (int64_t[]){L.line}, 1);
      if (id_loc > 0)
        id_locs[j++] = id_loc;
    }
    int64_t sample_val[max_watchers] = {0};
    sample_val[pos] = pes->period;
    pprof_sampleAdd(dp, sample_val, pctx->num_watchers, id_locs, us->idx);
    break;

  case PERF_RECORD_MMAP:;
    perf_event_mmap *map = (perf_event_mmap *)hdr;
    if (!(map->header.misc & PERF_RECORD_MISC_MMAP_DATA)) {
      LG_DBG("[PERF]<%d>(MAP)%d: %s (%lx/%lx/%lx)", pos, map->pid,
             map->filename, map->addr, map->len, map->pgoff);
      DsoIn in = *(DsoIn *)&map->addr;
      in.filename = map->filename;
      pid_add(map->pid, &in);
    }
    break;
  case PERF_RECORD_LOST:;
    perf_event_lost *lost = (perf_event_lost *)hdr;
    LG_DBG("[PERF]<%d>(LOST) %ld", pos, lost->lost);
    break;
  case PERF_RECORD_COMM:;
    perf_event_comm *comm = (perf_event_comm *)hdr;
    if (comm->header.misc & PERF_RECORD_MISC_COMM_EXEC) {
      LG_DBG("[PERF]<%d>(COMM)%d", pos, comm->pid);
      pid_free(comm->pid);
    }
    break;
  case PERF_RECORD_EXIT:;
    perf_event_exit *ext = (perf_event_exit *)hdr;
    LG_DBG("[PERF]<%d>(EXIT)%d", pos, ext->pid);
    pid_free(ext->pid);
    break;
  case PERF_RECORD_FORK:;
    perf_event_fork *frk = (perf_event_fork *)hdr;
    if (frk->ppid == frk->pid)
      ; // TODO
    else {
      LG_DBG("[PERF]<%d>(FORK)%d -> %d", pos, frk->ppid, frk->pid);
      pid_fork(frk->ppid, frk->pid);
    }
    break;

  default:
    break;
  }

  // Click the timer at the end of processing, since we always add the sampling
  // rate to the last time.
  int64_t now = now_nanos();

  if (now > pctx->send_nanos) {
    statsd_upload_globals(pctx);
    export(pctx, now);
  }
}

/*********************************  Printers  *********************************/
#define X_HLPK(a, b, c, d, e, f, g, h) "  -" #c ", --" #b ", (envvar: " #a ")",

// We use a non-NULL definition for an undefined string, because it's important
// that this table is always populated intentionally.  This is checked in the
// loop inside print_help()
// clang-format off
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
  [DD_PROFILING_ENABLED] =
"    Whether to enable DataDog profiling.  If this is true, then "MYNAME" as well\n"
"    as any other DataDog profilers are enabled.  If false, they are all disabled.\n"
"    Note: if this is set, the native profiler will set the DD_PROFILING_ENABLED\n"
"    environment variable in all sub-environments, thereby enabling DataDog profilers.\n"
"    default: on\n",
  [DD_PROFILING_NATIVE_ENABLED] =
"    Whether to enable "MYNAME" specifically, without altering how other DataDog\n"
"    profilers are run.  For example, DD_PROFILING_ENABLED can be used to disable\n"
"    an inner profile, whilst setting DD_PROFILING_NATIVE_ENABLED to enable "MYNAME"\n",
  [DD_PROFILING_COUNTSAMPLES] = STR_UNDF,
  [DD_PROFILING_UPLOAD_PERIOD] =
"    In seconds, how frequently to upload gathered data to Datadog.\n"
"    Currently, it is recommended to keep this value to 60 seconds, which is\n"
"    also the default.\n",
  [DD_PROFILE_NATIVEPROFILER] = STR_UNDF,
  [DD_PROFILING_] = STR_UNDF,
  [DD_PROFILING_NATIVEPRINTARGS] =
"    Whether or not to print configuration parameters to the trace log.  Can\n"
"    be `yes` or `no` (default: `no`).\n",
  [DD_PROFILING_NATIVEFAULTINFO] =
"    If "MYNAME" encounters a critical error, print a backtrace of internal\n"
"    functions for diagnostic purposes.  Values are `on` or `off`\n"
"    (default: off)\n",
  [DD_PROFILING_NATIVELOGMODE] =
"    One of `stdout`, `stderr`, `syslog`, or `disabled`.  Default is `stdout`.\n"
"    If a value is given but it does not match the above, it is treated as a\n"
"    filesystem path and a log will be appended there.  Log files are not\n"
"    cleared between runs and a service restart is needed for log rotation.\n",
  [DD_PROFILING_NATIVELOGLEVEL] =
"    One of `debug`, `notice`, `warn`, `error`.  Default is `warn`.\n",
  [DD_PROFILING_NATIVESENDFINAL] =
"    Determines whether to emit the last partial export if the instrumented\n"
"    process ends.  This is almost never useful.  Default is `no`.\n",
  [DD_PROFILING_NATIVETARGET] =
"    Instrument the given PID rather than launching a new process.\n",
  [DD_PROFILING_NATIVEGLOBAL] =
"    Instruments the whole system.  Overrides DD_PROFILING_NATIVETARGET.\n",
};
// clang-format on

char *help_key[DD_KLEN] = {OPT_TABLE(X_HLPK)};

// clang-format off
void print_help() {
  char help_hdr[] = ""
" usage: "MYNAME" [--help] [PROFILER_OPTIONS] COMMAND [COMMAND_ARGS]\n"
" eg: "MYNAME" -A hunter2 -H localhost -P 8192 redis-server /etc/redis/redis.conf\n\n";

  char help_opts_extra[] =
"  -e, --event:\n"
"    A string representing the events to sample.  Defaults to `cw`\n"
"    See the `events` section below for more details.\n"
"    eg: --event sCPU --event hREF\n\n"
"  -v, --version:\n"
"    Prints the version of "MYNAME" and exits.\n\n";

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
  printf("%s", help_opts_extra);
  printf("%s", help_events);
  for (int i = 0; i < num_perfs; i++)
    printf("%-10s - %-15s (%s, %s)\n", perfoptions[i].key,
           perfoptions[i].desc, perfoptions[i].label, perfoptions[i].unit);
}
// clang-format on

/*****************************  SIGSEGV Handler *******************************/
void sigsegv_handler(int sig, siginfo_t *si, void *uc) {
  // TODO this really shouldn't call printf-family functions...
  (void)uc;
  static void *buf[4096] = {0};
  size_t sz = backtrace(buf, 4096);
  fprintf(stderr, "[DDPROF]<%s> has encountered an error and will exit\n",
          str_version());
  if (sig == SIGSEGV)
    printf("[DDPROF] Fault address: %p\n", si->si_addr);
  backtrace_symbols_fd(buf, sz, STDERR_FILENO);
  exit(-1);
}

/*************************  Instrumentation Helpers  **************************/
// This is a quick-and-dirty implementation.  Ideally, we'll harmonize this
// with the other functions.
void instrument_pid(DDProfContext *ctx, pid_t pid) {
  perfopen_attr perf_funs = {.msg_fun = ddprof_callback,
                             .timeout_fun = ddprof_timeout};
  struct PEvent pes[100] = {0};
  int k = 0;
  for (int i = 0; i < ctx->num_watchers && ctx->params.enable; i++) {
    for (int j = 0; j < num_cpu; j++) {
      pes[k].fd = perfopen(pid, &ctx->watchers[i], j, false);
      pes[k].pos = i;
      if (!(pes[k].region = perfown(pes[k].fd))) {
        close(pes[k].fd);
        pes[k].fd = -1;
        LG_ERR("Could not finalize watcher %d.%d: registration (%s)", i, j,
               strerror(errno));
      }
      k++;
    }
  }

  LG_NTC("Entering main loop");
  // Setup signal handler if defined
  if (ctx->params.faultinfo)
    sigaction(SIGSEGV,
              &(struct sigaction){.sa_sigaction = sigsegv_handler,
                                  .sa_flags = SA_SIGINFO},
              NULL);

  // Perform initialization operations
  ctx->send_nanos = now_nanos() + ctx->params.upload_period * 1000000000;
  unwind_init(ctx->us);
  elf_version(EV_CURRENT); // Initialize libelf
  statsd_init();

  // Just before we enter the main loop, force the enablement of the perf
  // contexts
  for (int i = 0; i < ctx->num_watchers * num_cpu; i++) {
    if (-1 == ioctl(pes[i].fd, PERF_EVENT_IOC_ENABLE))
      LG_WRN("Couldn't enable watcher %d", i);
  }

  // Enter the main loop -- this will not return unless there is an error.
  main_loop(pes, ctx->num_watchers * num_cpu, &perf_funs, ctx);

  // If we're here, the main loop closed--probably the profilee closed
  if (errno)
    LG_WRN("Profiling context no longer valid (%s)", strerror(errno));
  else
    LG_WRN("Profiling context no longer valid");

  // We're going to close down, but first check whether we have a valid export
  // to send (or if we requested the last partial export with sendfinal)
  int64_t now = now_nanos();
  if (now > ctx->send_nanos || ctx->sendfinal) {
    LG_WRN("Sending final export");
    export(ctx, now);
  }
}

/******************************  Entrypoint  **********************************/
int main(int argc, char **argv) {
  //---- Inititiate structs
  int c = 0, oi = 0;
  DDProfContext *ctx =
      &(DDProfContext){.ddr = &(DDReq){.user_agent = "libddprof001",
                                       .language = "native",
                                       .family = "native"},
                       .dp = &(DProf){0},
                       .us = &(struct UnwindState){0}};
  DDReq *ddr = ctx->ddr;

  struct option lopts[] = {OPT_TABLE(X_LOPT){"event", 1, 0, 'e'},
                           {"help", 0, 0, 'h'},
                           {"version", 0, 0, 'v'}};

  //---- Populate default values
  OPT_TABLE(X_DFLT);
  bool default_watchers = true;
  ctx->num_watchers = 1;
  ctx->watchers[0] = perfoptions[10];

  //---- Process Options
  if (argc <= 1) {
    OPT_TABLE(X_FREE);
    print_help();
    return 0;
  }
  while (
      -1 !=
      (c = getopt_long(argc, argv, "+" OPT_TABLE(X_OSTR) "e:hv", lopts, &oi))) {
    switch (c) {
      OPT_TABLE(X_CASE)
    case 'e':;
      bool event_matched = false;
      for (int i = 0; i < num_perfs; i++) {
        size_t sz_opt = strlen(optarg);
        size_t sz_key = strlen(perfoptions[i].key);
        if (!strncmp(perfoptions[i].key, optarg, sz_key)) {

          // If we got a match, then we need to use non-default accounting
          if (default_watchers) {
            default_watchers = false;
            ctx->num_watchers = 0;
          }

          ctx->watchers[ctx->num_watchers] = perfoptions[i];

          double sample_period = 0;
          if (sz_opt > sz_key && optarg[sz_opt] == ',')
            sample_period = strtod(&optarg[sz_key + 1], NULL);
          if (sample_period > 0)
            ctx->watchers[ctx->num_watchers].sample_period = sample_period;
          ctx->num_watchers++;

          // Early exit
          event_matched = true;
          break;
        }
      }
      if (!event_matched) {
        LG_WRN("Event %s did not match any events", optarg);
      }
      break;
    case 'h':;
      OPT_TABLE(X_FREE);
      print_help();
      return 0;
    case 'v':;
      OPT_TABLE(X_FREE);
      print_version();
      return 0;
    default:;
      OPT_TABLE(X_FREE);
      LG_ERR("Invalid option %c", c);
      return -1;
    }
  }

  /****************************************************************************\
  |                            Process Arguments                               |
  \****************************************************************************/
  // Set defaults
  ctx->params.enable = true;
  ctx->params.upload_period = 60.0;

  // Process enable.  Note that we want the effect to hit an inner profile.
  // TODO das210603 do the semantics of this enablement match those used by
  //                other profilers?
  ctx->params.enable = !arg_yesno(ctx->enable, 0); // default yes
  if (ctx->params.enable)
    setenv("DD_PROFILING_ENABLED", "true", true);
  else
    setenv("DD_PROFILING_ENABLED", "false", true);

  // Process native profiler enablement override
  ctx->params.enable = !arg_yesno(ctx->native_enable, 0);

  // process upload_period
  if (ctx->upload_period) {
    double x = strtod(ctx->upload_period, NULL);
    if (x > 0.0)
      ctx->params.upload_period = x;
  }

  // Process faultinfo
  ctx->params.faultinfo = arg_yesno(ctx->faultinfo, 1); // default no

  // Process sendfinal
  ctx->params.sendfinal = arg_yesno(ctx->sendfinal, 1);

  // Process logging mode
  char const *logpattern[] = {"stdout", "stderr", "syslog", "disabled"};
  const int sizeOfLogpattern = 4;
  switch (arg_which(ctx->logmode, logpattern, sizeOfLogpattern)) {
  case -1:
  case 0:
    LOG_open(LOG_STDOUT, "");
    break;
  case 1:
    LOG_open(LOG_STDERR, "");
    break;
  case 2:
    LOG_open(LOG_SYSLOG, "");
    break;
  case 3:
    LOG_open(LOG_DISABLE, "");
    break;
  default:
    LOG_open(LOG_FILE, ctx->logmode);
    break;
  }

  // Process logging level
  char const *loglpattern[] = {"debug", "notice", "warn", "error"};
  const int sizeOfLoglpattern = 4;
  switch (arg_which(ctx->loglevel, loglpattern, sizeOfLoglpattern)) {
  case 0:
    LOG_setlevel(LL_DEBUG);
    break;
  case 1:
    LOG_setlevel(LL_NOTICE);
    break;
  case -1:
  case 2:
    LOG_setlevel(LL_WARNING);
    break;
  case 3:
    LOG_setlevel(LL_ERROR);
    break;
  }

  // process count_samples
  ctx->params.count_samples = arg_yesno(ctx->count_samples, 1); // default no

  // Adjust target PID
  pid_t pid_tmp = 0;
  if (ctx->pid && (pid_tmp = strtol(ctx->pid, NULL, 10)))
    ctx->params.pid = pid_tmp;

  // Adjust global mode
  ctx->params.global = arg_yesno(ctx->global, 1); // default no
  if (ctx->params.global)
    ctx->params.pid = -1;

  // Process input printer (do this right before argv/c modification)
  if (ctx->printargs && arg_yesno(ctx->printargs, 1)) {
    if (LOG_getlevel() < LL_DEBUG)
      LG_WRN("printarg specified, but loglevel too low to emit parameters");
    LG_DBG("Printing parameters");
    OPT_TABLE(X_PRNT);

    LG_DBG("Native profiler enabled: %s",
           ctx->params.enable ? "true" : "false");

    // Tell the user what mode is being used
    LG_DBG("Profiling mode: %s",
           -1 == ctx->params.pid ? "global"
               : pid_tmp         ? "target"
                                 : "wrapper");

    // Show watchers
    LG_DBG("Instrumented with %d watchers:", ctx->num_watchers);
    for (int i = 0; i < ctx->num_watchers; i++) {
      LG_DBG("  ID: %s, Pos: %d, Index: %lu, Label: %s, Mode: %d",
             ctx->watchers[i].key, i, ctx->watchers[i].config,
             ctx->watchers[i].label, ctx->watchers[i].mode);
      LG_DBG("Done printing parameters");
    }
  }

  // Adjust input parameters for execvp() (we do this even if unnecessary)
  argv += optind;
  argc -= optind;

  // Only throw an error if we needed the user to pass an arg
  if (ctx->params.pid) {
    if (ctx->params.pid == -1)
      LG_NTC("Instrumenting whole system");
    else
      LG_NTC("Instrumenting PID %d", ctx->params.pid);
  } else if (argc <= 0) {
    OPT_TABLE(X_FREE);
    LG_ERR("No target specified, exiting");
    return -1;
  }

  /****************************************************************************\
  |                             Run the Profiler                               |
  \****************************************************************************/
  // If the profiler was disabled, just skip ahead
  if (!ctx->params.enable) {
    LG_NTC("Profiling disabled");
    goto EXECUTE;
  }
  // Initialize the request object
  DDR_init(ddr);

  // Initialize the pprof
  for (int i = 0; i < ctx->num_watchers; i++) {
    pprof_labels[i] = ctx->watchers[i].label;
    pprof_units[i] = ctx->watchers[i].unit;
  }

  if (!pprof_Init(ctx->dp, (const char **)pprof_labels,
                  (const char **)pprof_units, ctx->num_watchers)) {
    OPT_TABLE(X_FREE);
    DDR_free(ddr);
    LG_ERR("Failed to initialize profiling storage");
    return -1;
  }
  pprof_timeUpdate(ctx->dp); // Set the time

  // Get the number of CPUs
  num_cpu = get_nprocs();

  // If I'm not being called as a wrapper, don't act like a wrapper.
  if (ctx->params.pid) {
    instrument_pid(ctx, ctx->params.pid);
    OPT_TABLE(X_FREE);
    DDR_free(ddr);
    unwind_free(ctx->us);
    pprof_Free(ctx->dp);
    return 0;
  }

  // Instrument the profiler
  // 1.  fork()
  // 2.  I am the original process.  Turn into target right away
  // 3.  I am the child.  Fork again and die
  // 4.  Instrument the original pid

  pid_t pid_target = getpid();
  pid_t child_pid = fork();
  if (!child_pid) {
    // 3.  I am the child
    if (fork()) {
      // Still have to cleanup, though...
      OPT_TABLE(X_FREE);
      DDR_free(ddr);
      unwind_free(ctx->us);
      pprof_Free(ctx->dp);
      return 0;
    }

    // In the future, we can probably harmonize the various exit paths by using
    // the same instrument_pid for global mode, targeted PID mode, and regular
    // instrumentation.
    instrument_pid(ctx, pid_target);

    // We are only here if the profiler fails or exits
    OPT_TABLE(X_FREE);
    DDR_free(ddr);
    unwind_free(ctx->us);
    pprof_Free(ctx->dp);

    LG_WRN("Profiling terminated");
    return -1;
  } else {
    // 2.  I am the target process.  Wait for my immediate child to close
    waitpid(child_pid, NULL, 0);
  }

EXECUTE:
  if (-1 == execvp(*argv, argv)) {
    switch (errno) {
    case ENOENT:
      LG_ERR("%s: file not found", argv[0]);
      break;
    case ENOEXEC:
    case EACCES:
      LG_ERR("%s: permission denied", argv[0]);
      break;
    default:
      LG_WRN("%s: failed to execute (%s)", argv[0], strerror(errno));
      break;
    }
  }

  // These are cleaned by execvp(), but we remove them here since this is the
  // error path and we don't want static analysis to report leaks.
  OPT_TABLE(X_FREE);
  DDR_free(ddr);
  pprof_Free(ctx->dp);
  return -1;
}
