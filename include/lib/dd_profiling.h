// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Starts profiling. Returns 0 on success, -1 on error.
//
// When called after a prior ddprof_stop_profiling(), the caller must ensure
// the application is quiescent w.r.t. heap/mmap activity for the restart to
// be safe -- see ddprof_stop_profiling() for the rationale.
__attribute__((__visibility__("default"))) int ddprof_start_profiling();

// Stops profiling. Waits up to `timeout_ms` for the profiler daemon to exit.
//
// In-flight allocations:
//   This call restores the GOT entries patched at start time so that no new
//   allocation goes through ddprof's hooks. It does NOT wait for hooks that
//   are already executing on other threads to finish. In the common "start
//   once at process init, stop once at shutdown" pattern this is safe.
//
//   If you intend to RESTART profiling (stop followed by another
//   ddprof_start_profiling()), the caller is responsible for ensuring no
//   allocation/mmap/free hook is still in flight from the previous session
//   when the next start runs -- otherwise those in-flight hooks can race
//   with the re-initialization of internal state (ring buffer remap,
//   tracker fields). In practice: quiesce allocation-heavy threads, or
//   insert a short delay between stop and start.
__attribute__((__visibility__("default"))) void
ddprof_stop_profiling(int timeout_ms);

#ifdef __cplusplus
}
#endif