// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "lib_embedded_data.h"

#ifdef DDPROF_EMBEDDED_LIB_DATA
// NOLINTNEXTLINE(cert-dcl51-cpp)
extern const char _binary_libdd_profiling_embedded_so_start[];
// NOLINTNEXTLINE(cert-dcl51-cpp)
extern const char _binary_libdd_profiling_embedded_so_end[];
#else
// NOLINTNEXTLINE(cert-dcl51-cpp)
static const char _binary_libdd_profiling_embedded_so_start[] = {};
// NOLINTNEXTLINE(cert-dcl51-cpp)
static const char _binary_libdd_profiling_embedded_so_end[] = {};
#endif

#ifdef DDPROF_EMBEDDED_EXE_DATA
extern const char _binary_ddprof_start[]; // NOLINT(cert-dcl51-cpp)
extern const char _binary_ddprof_end[];   // NOLINT(cert-dcl51-cpp)
#else
static const char _binary_ddprof_start[] = {}; // NOLINT(cert-dcl51-cpp)
static const char _binary_ddprof_end[] = {};   // NOLINT(cert-dcl51-cpp)
#endif

EmbeddedData profiling_lib_data() {
  EmbeddedData data = {.data = _binary_libdd_profiling_embedded_so_start,
                       // cppcheck-suppress comparePointers
                       .size = _binary_libdd_profiling_embedded_so_end -
                           _binary_libdd_profiling_embedded_so_start};
  return data;
}

EmbeddedData profiler_exe_data() {
  EmbeddedData data = {.data = _binary_ddprof_start,
                       // cppcheck-suppress comparePointers
                       .size = _binary_ddprof_end - _binary_ddprof_start};
  return data;
}
