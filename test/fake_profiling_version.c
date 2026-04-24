// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

// Fake libdd_profiling-embedded.so that returns a wrong version.
// Used by version_mismatch-ut to verify the loader rejects it.

__attribute__((__visibility__("default")))
__attribute__((aligned(64))) __thread char ddprof_lib_state[4096];

const char *ddprof_profiling_version(void) { return "0.0.0+wrong"; }
int ddprof_start_profiling(void) { return -1; }
void ddprof_stop_profiling(int timeout_ms) { (void)timeout_ms; }
