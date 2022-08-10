// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

// Env variable used to mark profiler as active in library mode
// Prevents reactivation of the profiler in child processes
// (since profiler will follow children)
inline constexpr const char *k_profiler_active_env_variable =
    "DD_PROFILING_NATIVE_LIBRARY_ACTIVE";

// Env variable to request autostart of profiler in library mode
inline constexpr const char *k_profiler_auto_start_env_variable =
    "DD_PROFILING_NATIVE_AUTOSTART";

// Env variable to force use of embedded shared library
inline constexpr const char
    *k_profiler_use_embedded_libdd_profiling_env_variable =
        "DD_PROFILING_NATIVE_USE_EMBEDDED_LIB";

// Env variable to override ddprof exe used in library mode
// By default exe embedded in library is use
inline constexpr const char *k_profiler_ddprof_exe_env_variable =
    "DD_PROFILING_NATIVE_DDPROF_EXE";

// Env variable set by ddprof to pass socket to injected library
// for memory allocation profiling (when initiated in wrapper mode)
inline constexpr const char *k_profiler_lib_socket_env_variable =
    "DD_PROFILING_NATIVE_LIB_SOCKET";

// Env variable to override events to activate (-e option)
inline constexpr const char *k_events_env_variable =
    "DD_PROFILING_NATIVE_EVENTS";

inline constexpr const char *k_libdd_profiling_name = "libdd_profiling.so";
