#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <limits.h>

#define DD_COMMON_START_RANGE 1000
#define DD_NATIVE_START_RANGE 2000

#define EXPAND_ENUM(a, b) a,
#define EXPAND_ERROR_MESSAGE(a, b) b,

#define COMMOM_ERROR_TABLE(X)                                                  \
  X(DD_WHAT_UKNW, "UKNW: Undocumented error")                                  \
  X(DD_WHAT_BADALLOC, "BADALLOC: Allocation error")                            \
  X(DD_WHAT_STDEXCEPT, "STDEXCEPT: Standard exception caught")                 \
  X(DD_WHAT_UKNWEXCEPT, "UKNWEXCEPT: Unknown exception caught")

#define NATIVE_ERROR_TABLE(X)                                                  \
  X(DD_WHAT_DWFL_LIB_ERROR, "DWFL_LIB_ERROR: error withing dwfl library")      \
  X(DD_WHAT_UW_CACHE_ERROR, "UW_CACHE_ERROR: error from unwinding cache")      \
  X(DD_WHAT_UW_ERROR, "UW_ERROR: error from unwinding code")                   \
  X(DD_WHAT_CAPLIB, "CAPLIB: error when reading capabilities")                 \
  X(DD_WHAT_USERID, "USERID: error in user ID manipulations")                  \
  X(DD_WHAT_PERFOPEN, "PERFOPEN: error during perf_event_open")                \
  X(DD_WHAT_IOCTL, "PERFOPEN: error during perf_event_open")                   \
  X(DD_WHAT_PERFMMAP, "PERFMMAP: error in mmap operations")                    \
  X(DD_WHAT_MAINLOOP, "MAINLOOP: error in main_loop coordinator")              \
  X(DD_WHAT_POLLTIMEOUT, "POLLTIMEOUT: timeout when polling perf events")      \
  X(DD_WHAT_POLLERROR, "POLLERROR: Unknown poll error")                        \
  X(DD_WHAT_POLLHANGUP, "POLLHANGUP: perf event file descriptor hang up")      \
  X(DD_WHAT_WORKER_RESET,                                                      \
    "WORKER_RESET: worker reset requested (not a fatal error)")                \
  X(DD_WHAT_PROCSTATE, "PROCSTATE: error when retrieveing procstate")          \
  X(DD_WHAT_PPROF, "PPROF: error in pprof manipulations")                      \
  X(DD_WHAT_STATSD, "STATSD: statsd interface")                                \
  X(DD_WHAT_DDPROF_STATS, "DDPROF_STATS: error in stats module")               \
  X(DD_WHAT_EXPORTER, "EXPORTER")                                              \
  X(DD_WHAT_ARGUMENT, "ARGUMENT: Error writing arguments")                     \
  X(DD_WHAT_INPUT_PROCESS, "DD_WHAT_INPUT_PROCESS")                            \
  X(DD_WHAT_STACK_HANDLE, "DD_WHAT_STACK_HANDLE")                              \
  X(DD_WHAT_DSO, "DD_WHAT_DSO")                                                \
  X(DD_WHAT_UNHANDLED_DSO, "UNHANDLED_DSO: ignore dso type")                   \
  X(DD_WHAT_UNITTEST, "UNITTEST: unit test error")

// generic erno errors available from /usr/include/asm-generic/errno.h

enum DDRes_What {
  // erno starts after ELAST 106 as of now
  DD_WHAT_MIN_ERNO = DD_COMMON_START_RANGE,
  // common errors
  COMMOM_ERROR_TABLE(EXPAND_ENUM) COMMON_ERROR_SIZE,
  DD_WHAT_MIN_NATIVE = DD_NATIVE_START_RANGE,
  NATIVE_ERROR_TABLE(EXPAND_ENUM) NATIVE_ERROR_SIZE,
  // max
  DD_WHAT_MAX = SHRT_MAX,
};

/// Retrieve an explicit error message matching the error ID (from table above)
const char *ddres_error_message(int16_t what);

#ifdef __cplusplus
}
#endif
