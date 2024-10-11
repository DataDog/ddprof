// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <climits>
#include <cstdint>

enum : uint16_t { DD_COMMON_START_RANGE = 1000, DD_NATIVE_START_RANGE = 2000 };

#define EXPAND_ENUM(a, b) DD_WHAT_##a,
#define EXPAND_ERROR_MESSAGE(a, b) #a ": " b,

#define COMMON_ERROR_TABLE(X)                                                  \
  X(UKNW, "undocumented error")                                                \
  X(BADALLOC, "allocation error")                                              \
  X(STDEXCEPT, "standard exception caught")                                    \
  X(UKNWEXCEPT, "unknown exception caught")

#define NATIVE_ERROR_TABLE(X)                                                  \
  X(DWFL_LIB_ERROR, "error withing dwfl library")                              \
  X(UW_CACHE_ERROR, "error from unwinding cache")                              \
  X(UW_ERROR, "error from unwinding code")                                     \
  X(UW_MAX_PIDS, "Maximum number of PIDs reached")                             \
  X(UW_MAX_DEPTH, "max depth reached in unwinding")                            \
  X(CAPLIB, "error when reading capabilities")                                 \
  X(USERID, "error in user ID manipulations")                                  \
  X(MODULE, "error retrieving debug info in modules")                          \
  X(PEINIT, "error allocating space for pevent")                               \
  X(PERFOPEN, "error during perf_event_open")                                  \
  X(PERFRB, "error with perf_event ringbuffer")                                \
  X(PERFSAMP, "error processing perf sample")                                  \
  X(IOCTL, "error during perf_event_open")                                     \
  X(PERFMMAP, "error in mmap operations")                                      \
  X(MAINLOOP, "error in main_loop coordinator")                                \
  X(MAINLOOP_INIT, "error initializing the profiling mainloop")                \
  X(POLLTIMEOUT, "timeout when polling perf events")                           \
  X(POLLERROR, "Unknown poll error")                                           \
  X(POLLHANGUP, "perf event file descriptor hang up")                          \
  X(PROCSTATE, "error when retrieving process state")                          \
  X(PPROF, "error in pprof manipulations")                                     \
  X(STATSD, "statsd interface")                                                \
  X(DDPROF_STATS, "error in stats module")                                     \
  X(EXPORTER, "error exporting")                                               \
  X(EXPORT_TIMEOUT, "pending export failed to return in time")                 \
  X(ARGUMENT, "error writing arguments")                                       \
  X(INPUT_PROCESS, "")                                                         \
  X(DSO, "")                                                                   \
  X(JIT, "Error parsing JIT files")                                            \
  X(NO_JIT_FILE, "File not readable for JIT")                                  \
  X(UNHANDLED_CONFIG, "unhandled configuration")                               \
  X(UNHANDLED_DSO, "ignore dso type")                                          \
  X(WORKERLOOP_INIT, "error initializing the worker loop")                     \
  X(SYS_CONFIG, "error checking system configuration")                         \
  X(UNITTEST, "unit test error")                                               \
  X(SOCKET, "error during socket operation")                                   \
  X(TEMP_FILE, "error during temporary file creation")                         \
  X(CGROUP, "error while reading cgroup information")                          \
  X(TSC, "failed to setup TSC")                                                \
  X(INVALID_ELF, "invalid elf file")                                           \
  X(AMBIGUOUS_LOAD_SEGMENT, "ambiguous executable LOAD segment")               \
  X(SYMBOLIZER, "symbolizer error")                                            \
  X(NO_MATCHING_LOAD_SEGMENT, "unable to find a LOAD segment matching mapping")

// generic erno errors available from /usr/include/asm-generic/errno.h

enum DDRes_What : uint16_t {
  // errno starts after ELAST 106 as of now
  DD_WHAT_MIN_ERRNO = DD_COMMON_START_RANGE,
  // common errors
  COMMON_ERROR_TABLE(EXPAND_ENUM) COMMON_ERROR_SIZE,
  DD_WHAT_MIN_NATIVE = DD_NATIVE_START_RANGE,
  NATIVE_ERROR_TABLE(EXPAND_ENUM) NATIVE_ERROR_SIZE,
  // max
  DD_WHAT_MAX = SHRT_MAX,
};

/// Retrieve an explicit error message matching the error ID (from table above)
const char *ddres_error_message(int16_t what);
