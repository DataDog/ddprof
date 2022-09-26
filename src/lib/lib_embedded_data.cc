// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "lib_embedded_data.hpp"

#ifdef DDPROF_EMBEDDED_LIB_DATA
// NOLINTNEXTLINE cert-dcl51-cpp
extern const char _binary_libdd_profiling_embedded_so_start[];
// NOLINTNEXTLINE cert-dcl51-cpp
extern const char _binary_libdd_profiling_embedded_so_end[];
#else
// NOLINTNEXTLINE cert-dcl51-cpp
static const char _binary_libdd_profiling_embedded_so_start[] = {};
// NOLINTNEXTLINE cert-dcl51-cpp
static const char _binary_libdd_profiling_embedded_so_end[] = {};
#endif

#ifdef DDPROF_EMBEDDED_EXE_DATA
extern const char _binary_ddprof_start[]; // NOLINT cert-dcl51-cpp
extern const char _binary_ddprof_end[];   // NOLINT cert-dcl51-cpp
#else
static const char _binary_ddprof_start[] = {}; // NOLINT cert-dcl51-cpp
static const char _binary_ddprof_end[] = {};   // NOLINT cert-dcl51-cpp
#endif

namespace ddprof {
span<const std::byte> profiling_lib_data() {
  return as_bytes(ddprof::span{_binary_libdd_profiling_embedded_so_start,
                               _binary_libdd_profiling_embedded_so_end});
}

span<const std::byte> profiler_exe_data() {
  return as_bytes(ddprof::span{_binary_ddprof_start, _binary_ddprof_end});
}
} // namespace ddprof
