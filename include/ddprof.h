#pragma once

#include <ddprof/dd_send.h>
#include <execinfo.h>
#include <sys/time.h>

#include "ddprof_context.h"
#include "ipc.h"
#include "logger.h"
#include "perf.h"
#include "version.h"

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
#define X_ENUM(a, b, c, d, e, f, g, h) a,

//  A                              B                C   D   E  F         G     H
#define OPT_TABLE(XX)                                                                       \
  XX(DD_API_KEY,                   apikey,          A, 'A', 1, ctx->ddr, NULL, "")          \
  XX(DD_ENV,                       environment,     E, 'E', 1, ctx->ddr, NULL, "")          \
  XX(DD_AGENT_HOST,                host,            H, 'H', 1, ctx->ddr, NULL, "localhost") \
  XX(DD_SITE,                      site,            I, 'I', 1, ctx->ddr, NULL, "")          \
  XX(DD_TRACE_AGENT_PORT,          port,            P, 'P', 1, ctx->ddr, NULL, "8126")      \
  XX(DD_SERVICE,                   service,         S, 'S', 1, ctx->ddr, NULL, "myservice") \
  XX(DD_TAGS,                      tags,            T, 'T', 1, ctx,      NULL, "")          \
  XX(DD_VERSION,                   serviceversion,  V, 'V', 1, ctx->ddr, NULL, "")          \
  XX(DD_PROFILING_ENABLED,         enable,          d, 'd', 1, ctx,      NULL, "yes")       \
  XX(DD_PROFILING_NATIVE_ENABLED,  native_enable,   n, 'n', 1, ctx,      NULL, "yes")       \
  XX(DD_PROFILING_COUNTSAMPLES,    count_samples,   c, 'c', 1, ctx,      NULL, "yes")       \
  XX(DD_PROFILING_UPLOAD_PERIOD,   upload_period,   u, 'u', 1, ctx,      NULL, "60")        \
  XX(DD_PROFILING_WORKER_PERIOD,   worker_period,   w, 'w', 1, ctx,      NULL, "240")       \
  XX(DD_PROFILING_CACHE_PERIOD,    cache_period,    k, 'k', 1, ctx,      NULL, "15")        \
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

typedef enum DDKeys { OPT_TABLE(X_ENUM) DD_KLEN } DDKeys;
#undef X_ENUM

int statsd_init();

// Initialize a ctx
bool ddprof_ctx_init(DDProfContext *ctx);
void ddprof_ctx_free(DDProfContext *);
bool ddprof_ctx_watcher_process(DDProfContext *, char *);

/******************************  Perf Callback  *******************************/
DDRes reset_state(DDProfContext *, volatile bool *continue_profiling);
DDRes export(DDProfContext *, int64_t);
DDRes ddprof_timeout(volatile bool *, void *);

/*********************************  Printers  *********************************/
void print_help();

/*****************************  SIGSEGV Handler *******************************/
void sigsegv_handler(int, siginfo_t *, void *);

/*************************  Instrumentation Helpers  **************************/
void instrument_pid(DDProfContext *, pid_t, int);
void ddprof_setctx(DDProfContext *);
