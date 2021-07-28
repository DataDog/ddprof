#ifndef _H_ddprof
#define _H_ddprof

#include <ddprof/dd_send.h>
#include <execinfo.h>
#include <sys/time.h>

#include "ipc.h"
#include "logger.h"
#include "perf.h"
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
  char *coredumps;
  char *nice;
  char *sendfinal;
  char *pid;
  char *global;
  struct {
    bool count_samples;
    bool enable;
    double upload_period;
    bool profprofiler;
    bool faultinfo;
    bool coredumps;
    int nice;
    bool sendfinal;
    pid_t pid;
    bool global;
  } params;
  PerfOption watchers[max_watchers];
  int num_watchers;

  struct UnwindState *us;
  int64_t send_nanos; // Last time an export was sent
} DDProfContext;

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
// clang-format off
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
  XX(DD_PROFILING_NATIVEDUMPS,     coredumps,       m, 'm', 1, ctx,      NULL, "no")        \
  XX(DD_PROFILING_NATIVENICE,      nice,            i, 'i', 1, ctx,      NULL, "")          \
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
case casechar:                                                                 \
  if ((targ)->key)                                                             \
    free((void *)(targ)->key);                                                 \
  (targ)->key = strdup(optarg);                                                \
  break;

// TODO das210603 I don't think this needs to be inlined as a macro anymore
#define FREE_EXP(key, targ)                                                    \
  __extension__({                                                              \
    if ((targ)->key)                                                           \
      free((void *)(targ)->key);                                               \
    (targ)->key = NULL;                                                        \
  })

typedef enum DDKeys { OPT_TABLE(X_ENUM) DD_KLEN } DDKeys;

int statsd_init();
void statsd_upload_globals(DDProfContext *);
void print_diagnostics();

// Initialize a ctx
DDProfContext *ddprof_ctx_init();
void ddprof_ctx_free(DDProfContext *);
bool ddprof_ctx_watcher_process(DDProfContext *, char *);

/******************************  Perf Callback  *******************************/
bool reset_state(DDProfContext *);
void export(DDProfContext *, int64_t);
void ddprof_timeout(void *);
void ddprof_callback(struct perf_event_header *, int, void *);

/*********************************  Printers  *********************************/
void print_help();

/*****************************  SIGSEGV Handler *******************************/
void sigsegv_handler(int, siginfo_t *, void *);

/*************************  Instrumentation Helpers  **************************/
void instrument_pid(DDProfContext *, pid_t, int);
void ddprof_setctx(DDProfContext *);

#endif
