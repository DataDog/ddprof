#include "ddprof.h"

#include <ddprof/pprof.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <unistd.h>
#include <x86intrin.h>

#include "ddprofcmdline.h"
#include "procutils.h"
#include "statsd.h"
#include "unwind.h"

#define USERAGENT_DEFAULT "libddprof"
#define LANGUAGE_DEFAULT "native"
#define FAMILY_DEFAULT "native"

// This is pretty bad if we ever need two ddprof contexts!
DDProfContext *ddprof_ctx_init() {
  static DDProfContext ctx = {0};
  static DDReq ddr = {0};
  static UnwindState us = {0};
  static DProf dp = {0};

  ddr.user_agent = USERAGENT_DEFAULT;
  ddr.language = LANGUAGE_DEFAULT;
  ddr.family = FAMILY_DEFAULT;
  ctx.ddr = &ddr;
  ctx.dp = &dp;
  ctx.us = &us;
  return &ctx;
}

void ddprof_ctx_free(DDProfContext *ctx) {
  DDR_free(ctx->ddr);
  pprof_Free(ctx->dp);
}

// Account globals
unsigned long events_lost = 0;
unsigned long samples_recv = 0;
unsigned long ticks_unwind = 0;

static int fd_statsd = -1;

int statsd_init() {
  char *path_statsd = NULL;
  if ((path_statsd = getenv("DD_DOGSTATSD_SOCKET"))) {
    fd_statsd = statsd_open(path_statsd, strlen(path_statsd));
    if (-1 == fd_statsd) {
      return fd_statsd;
    }
  }
  return 0;
}

#define DDPN "datadog.profiler.native."
void statsd_upload_globals(DDProfContext *ctx) {
  static char key_rss[] = DDPN "rss";
  static char key_user[] = DDPN "utime";
  static char key_st_elements[] = DDPN "pprof.st_elements";
  static char key_ticks_unwind[] = DDPN "unwind.ticks";
  static char key_events_lost[] = DDPN "events.lost";
  static char key_samples_recv[] = DDPN "samples.recv";

  // If there's nothing that can be done, then there's nothing to do.
  if (-1 == fd_statsd)
    return;

  // Upload ddprof's procfs values
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

  // Upload global gauges
  uint64_t st_size = ctx->dp->string_table_size(ctx->dp->string_table_data);
  statsd_send(fd_statsd, key_st_elements, &(long){st_size}, STAT_GAUGE);
  statsd_send(fd_statsd, key_ticks_unwind, &(long){ticks_unwind}, STAT_GAUGE);
  statsd_send(fd_statsd, key_events_lost, &(long){events_lost}, STAT_GAUGE);
  statsd_send(fd_statsd, key_samples_recv, &(long){samples_recv}, STAT_GAUGE);
}

void print_diagnostics() {
  LG_NTC("[STATS] ticks_unwind: %lu", ticks_unwind);
  LG_NTC("[STATS] events_lost: %lu", events_lost);
  LG_NTC("[STATS] samples_recv: %lu", samples_recv);
}

// Internal functions
static inline int64_t now_nanos() {
  static struct timeval tv = {0};
  gettimeofday(&tv, NULL);
  return (tv.tv_sec * 1000000 + tv.tv_usec) * 1000;
}

/******************************  Perf Callback  *******************************/
void export(DDProfContext *ctx, int64_t now) {
  DDReq *ddr = ctx->ddr;
  DProf *dp = ctx->dp;

  // Before any state gets reset, export metrics to statsd
  statsd_upload_globals(ctx);

  // And emit diagnostic output (if it's enabled)
  print_diagnostics();

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
  ctx->send_nanos += ctx->params.upload_period * 1000000000;

  // Prepare pprof for next window
  pprof_timeUpdate(dp);

  // We're done exporting, so finish by clearing out any global gauges
  ticks_unwind = 0;
  events_lost = 0;
  samples_recv = 0;
}

void ddprof_timeout(void *arg) {
  DDProfContext *ctx = arg;
  int64_t now = now_nanos();

  if (now > ctx->send_nanos)
    export(ctx, now);
}

typedef union flipper {
  uint64_t full;
  uint32_t half[2];
} flipper;

perf_event_sample *hdr2samp(struct perf_event_header *hdr) {
  static perf_event_sample sample = {0};
  memset(&sample, 0, sizeof(sample));

  uint64_t *buf = (uint64_t *)(hdr + 1);

  if (PERF_SAMPLE_IDENTIFIER & DEFAULT_SAMPLE_TYPE) {
    sample.sample_id = (uint64_t)*buf++;
  }
  if (PERF_SAMPLE_IP & DEFAULT_SAMPLE_TYPE) {
    sample.ip = (uint64_t)*buf++;
  }
  if (PERF_SAMPLE_TID & DEFAULT_SAMPLE_TYPE) {
    sample.pid = ((flipper *)buf)->half[0];
    sample.tid = ((flipper *)buf)->half[1];
    buf++;
  }
  if (PERF_SAMPLE_TIME & DEFAULT_SAMPLE_TYPE) {
    sample.time = *(uint64_t *)buf++;
  }
  if (PERF_SAMPLE_ADDR & DEFAULT_SAMPLE_TYPE) {
    sample.addr = *(uint64_t *)buf++;
  }
  if (PERF_SAMPLE_ID & DEFAULT_SAMPLE_TYPE) {
    sample.id = *(uint64_t *)buf++;
  }
  if (PERF_SAMPLE_STREAM_ID & DEFAULT_SAMPLE_TYPE) {
    sample.stream_id = *(uint64_t *)buf++;
  }
  if (PERF_SAMPLE_CPU & DEFAULT_SAMPLE_TYPE) {
    sample.cpu = ((flipper *)buf)->half[0];
    sample.res = ((flipper *)buf)->half[1];
    buf++;
  }
  if (PERF_SAMPLE_PERIOD & DEFAULT_SAMPLE_TYPE) {
    sample.period = *(uint64_t *)buf++;
  }
  if (PERF_SAMPLE_READ & DEFAULT_SAMPLE_TYPE) {
    // sizeof(uint64_t) == sizeof(ptr)
    sample.v = (struct read_format *)buf++;
  }
  if (PERF_SAMPLE_CALLCHAIN & DEFAULT_SAMPLE_TYPE) {
    sample.nr = *(uint64_t *)buf++;
    sample.ips = (uint64_t *)buf;
    buf += sample.nr;
  }
  if (PERF_SAMPLE_RAW & DEFAULT_SAMPLE_TYPE) {}
  if (PERF_SAMPLE_BRANCH_STACK & DEFAULT_SAMPLE_TYPE) {}
  if (PERF_SAMPLE_REGS_USER & DEFAULT_SAMPLE_TYPE) {
    sample.abi = *(uint64_t *)buf++;
    sample.regs = (uint64_t *)buf;
    buf += 3; // TODO make this more generic?
  }
  if (PERF_SAMPLE_STACK_USER & DEFAULT_SAMPLE_TYPE) {
    sample.size_stack = *(uint64_t *)buf++;
    if (sample.size_stack) {
      sample.data_stack = (char *)buf;
      buf = (void *)buf + sample.size_stack;
    } else {
      // Not sure
    }
  }
  if (PERF_SAMPLE_WEIGHT & DEFAULT_SAMPLE_TYPE) {}
  if (PERF_SAMPLE_DATA_SRC & DEFAULT_SAMPLE_TYPE) {}
  if (PERF_SAMPLE_TRANSACTION & DEFAULT_SAMPLE_TYPE) {}
  if (PERF_SAMPLE_REGS_INTR & DEFAULT_SAMPLE_TYPE) {}

  return &sample;
}

void ddprof_pr_sample(DDProfContext *ctx, struct perf_event_header *hdr,
                      int pos) {
  // Before we do anything else, copy the perf_event_header into a sample
  perf_event_sample *sample = hdr2samp(hdr);
  static uint64_t id_locs[MAX_STACK] = {0};
  struct UnwindState *us = ctx->us;
  DProf *dp = ctx->dp;
  ++samples_recv;
  us->pid = sample->pid;
  us->idx = 0; // Modified during unwinding; has stack depth
  us->stack = NULL;
  us->stack_sz = sample->size_stack;
  us->stack = sample->data_stack;
  memcpy(&us->regs[0], sample->regs, 3 * sizeof(uint64_t));
  us->max_stack = MAX_STACK;
  FunLoc_clear(us->locs);
  unsigned long this_ticks_unwind = __rdtsc();
  if (-1 == unwindstate__unwind(us)) {
    Dso *dso = dso_find(us->pid, us->eip);
    if (!dso) {
      LG_WRN("Error getting map for [%d](0x%lx)", us->pid, us->eip);
      analyze_unwinding_error(us->pid, us->eip);
    } else {
      LG_WRN("Error unwinding %s [%d](0x%lx)", dso_path(dso), us->pid, us->eip);
    }
    return;
  }
  ticks_unwind += __rdtsc() - this_ticks_unwind;
  FunLoc *locs = us->locs;
  for (uint64_t i = 0, j = 0; i < us->idx; i++) {
    FunLoc L = locs[i];
    uint64_t id_map, id_fun, id_loc;

    // Using the sopath instead of srcpath in locAdd for the DD UI
    id_map = pprof_mapAdd(dp, L.map_start, L.map_end, L.map_off, L.sopath, "");
    id_fun = pprof_funAdd(dp, L.funname, L.funname, L.srcpath, 0);
    id_loc = pprof_locAdd(dp, id_map, 0, (uint64_t[]){id_fun},
                          (int64_t[]){L.line}, 1);
    if (id_loc > 0)
      id_locs[j++] = id_loc;
  }
  int64_t sample_val[max_watchers] = {0};
  sample_val[pos] = sample->period;
  pprof_sampleAdd(dp, sample_val, ctx->num_watchers, id_locs, us->idx);
}

void ddprof_pr_mmap(DDProfContext *ctx, perf_event_mmap *map, int pos) {
  (void)ctx;
  if (!(map->header.misc & PERF_RECORD_MISC_MMAP_DATA)) {
    LG_DBG("[PERF]<%d>(MAP)%d: %s (%lx/%lx/%lx)", pos, map->pid, map->filename,
           map->addr, map->len, map->pgoff);
    DsoIn in = *(DsoIn *)&map->addr;
    in.filename = map->filename;
    pid_add(map->pid, &in);
  }
}

void ddprof_pr_lost(DDProfContext *ctx, perf_event_lost *lost, int pos) {
  (void)ctx;
  (void)pos;
  events_lost += lost->lost;
}

void ddprof_pr_comm(DDProfContext *ctx, perf_event_comm *comm, int pos) {
  (void)ctx;
  if (comm->header.misc & PERF_RECORD_MISC_COMM_EXEC) {
    LG_DBG("[PERF]<%d>(COMM)%d", pos, comm->pid);
    pid_free(comm->pid);
  }
}

void ddprof_pr_fork(DDProfContext *ctx, perf_event_fork *frk, int pos) {
  (void)ctx;
  LG_DBG("[PERF]<%d>(FORK)%d -> %d", pos, frk->ppid, frk->pid);
  if (frk->ppid != frk->pid) {
    pid_fork(frk->ppid, frk->pid);
  } else {
    pid_free(frk->pid);
    pid_backpopulate(frk->pid);
  }
}

void ddprof_pr_exit(DDProfContext *ctx, perf_event_exit *ext, int pos) {
  (void)ctx;
  LG_DBG("[PERF]<%d>(EXIT)%d", pos, ext->pid);
  pid_free(ext->pid);
}

void ddprof_callback(struct perf_event_header *hdr, int pos, void *arg) {
  DDProfContext *ctx = arg;

  switch (hdr->type) {
  case PERF_RECORD_SAMPLE:
    ddprof_pr_sample(ctx, hdr, pos);
    break;
  case PERF_RECORD_MMAP:
    ddprof_pr_mmap(ctx, (perf_event_mmap *)hdr, pos);
    break;
  case PERF_RECORD_LOST:
    ddprof_pr_lost(ctx, (perf_event_lost *)hdr, pos);
    break;
  case PERF_RECORD_COMM:
    ddprof_pr_comm(ctx, (perf_event_comm *)hdr, pos);
    break;
  case PERF_RECORD_EXIT:
    ddprof_pr_exit(ctx, (perf_event_exit *)hdr, pos);
    break;
  case PERF_RECORD_FORK:
    ddprof_pr_fork(ctx, (perf_event_fork *)hdr, pos);
    break;
  default:
    break;
  }

  // Click the timer at the end of processing, since we always add the sampling
  // rate to the last time.
  int64_t now = now_nanos();

  if (now > ctx->send_nanos) {
    export(ctx, now);
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
  [DD_PROFILING_NATIVEDUMPS] =
"    Whether "MYNAME" is able to emit coredumps on failure.\n"
"    (default: off)\n",
  [DD_PROFILING_NATIVENICE] =
"    Sets the nice level of "MYNAME" without affecting any instrumented\n"
"    processes.  This is useful on small containers with spiky workloads.\n"
"    If this parameter isn't given, then the nice level is unchanged.\n",
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

  static int num_perfs = sizeof(perfoptions) / sizeof(*perfoptions);
  for (int i = 0; i < num_perfs; i++)
    printf("%-10s - %-15s (%s, %s)\n", perfoptions_lookup[i],
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
void instrument_pid(DDProfContext *ctx, pid_t pid, int num_cpu) {
  perfopen_attr perf_funs = {.msg_fun = ddprof_callback,
                             .timeout_fun = ddprof_timeout};
  struct PEvent pes[100] = {0};
  int k = 0;
  for (int i = 0; i < ctx->num_watchers && ctx->params.enable; i++) {
    for (int j = 0; j < num_cpu; j++) {
      pes[k].fd = perfopen(pid, &ctx->watchers[i], j, true);
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

  // Disable core dumps (unless enabled)
  if (!ctx->params.coredumps)
    setrlimit(RLIMIT_CORE, 0);

  // Set the nice level, but only if it was overridden because 0 is valid
  if (ctx->nice) {
    setpriority(PRIO_PROCESS, 0, ctx->params.nice);
    if (errno) {
      LG_WRN("Requested nice level (%d) could not be set", ctx->params.nice);
    }
  }

  // Perform initialization operations
  ctx->send_nanos = now_nanos() + ctx->params.upload_period * 1000000000;

  bool statusOK = unwind_init(ctx->us);
  if (!statusOK) {
    LG_ERR("Error when initializing unwinding");
    return;
  }

  if (statsd_init() == -1) {
    LG_WRN("Error from statsd_init");
  }

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

    // The cache-clearing code should be fairly robust, but in the chance that
    // it fails (perhaps if it includes dealloc->alloc in a library), then
    // we can no longer provide service.  All we can do is emit an error and
    // cleanup
    if (!dwfl_caches_clear(ctx->us)) {
      LG_ERR("[DDPROF] Error refreshing unwinding module, profiling shutdown");
      return;
    }
  }
  unwind_free(ctx->us);
}

/****************************  Argument Processor  ***************************/
void ddprof_setctx(DDProfContext *ctx) {
  // If events are set, install default watcher
  if (!ctx->num_watchers) {
    ctx->num_watchers = 1;
    ctx->watchers[0] = perfoptions[10];
  }

  // Set defaults
  ctx->params.enable = true;
  ctx->params.upload_period = 60.0;

  // Process enable.  Note that we want the effect to hit an inner profile.
  // TODO das210603 do the semantics of this match other profilers?
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

  // Process coredumps
  // This probably makes no sense with faultinfo enabled, but considering that
  // there are other dumpable signals, we ignore
  ctx->params.coredumps = arg_yesno(ctx->coredumps, 1); // default no

  // Process nice level
  if (ctx->nice) {
    char *ptr_nice = ctx->nice;
    int tmp_nice = strtol(ctx->nice, &ptr_nice, 10);
    if (ptr_nice != ctx->nice)
      ctx->params.nice = tmp_nice;
  }

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
             ctx->watchers[i].desc, i, ctx->watchers[i].config,
             ctx->watchers[i].label, ctx->watchers[i].mode);
      LG_DBG("Done printing parameters");
    }
  }
}
