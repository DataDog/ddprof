// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

// Test that libdd_profiling.so (the loader) works when loaded at runtime
// with RTLD_GLOBAL.
//
// This reproduces the pattern used by applications that dlopen the profiling
// library at startup (e.g., after reading a config flag).
// The loader's constructor extracts and dlopen's the embedded .so, which
// references ddprof_lib_state as an extern symbol defined in the loader.
// On glibc, RTLD_GLOBAL only takes effect after dlopen returns, so without
// the self-promotion fix the embedded library fails with:
//   "undefined symbol: ddprof_lib_state"
//
// Note: this test is skipped on musl. RTLD_GLOBAL dlopen of the loader is
// unsupported on musl because musl rejects initial-exec TLS cross-library
// relocations for dlopen'd libraries entirely.

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

typedef int (*start_fn_t)(void);
typedef void (*stop_fn_t)(int);

int main(void) {
  const char *lib_path = getenv("TEST_DD_PROFILING_LIB");
  if (!lib_path) {
    lib_path = "./libdd_profiling.so";
  }

  fprintf(stderr, "loading %s with RTLD_LAZY | RTLD_GLOBAL...\n", lib_path);

  void *handle = dlopen(lib_path, RTLD_LAZY | RTLD_GLOBAL);
  if (!handle) {
    fprintf(stderr, "FAIL: dlopen: %s\n", dlerror());
    return 1;
  }

  // Call ddprof_start_profiling. If the embedded library failed to load
  // (the bug we're testing for), this returns -1.
  start_fn_t start = (start_fn_t)dlsym(handle, "ddprof_start_profiling");
  if (!start) {
    fprintf(stderr, "FAIL: ddprof_start_profiling not found\n");
    dlclose(handle);
    return 1;
  }

  int rc = start();
  // The profiler may fail to start for environment reasons (no perf events,
  // etc.), but a return of -1 specifically means the embedded library was
  // never loaded (the loader's start function returns -1 when its function
  // pointer is NULL).
  if (rc == -1) {
    fprintf(stderr,
            "FAIL: ddprof_start_profiling returned -1 "
            "(embedded library not loaded)\n");
    dlclose(handle);
    return 1;
  }

  stop_fn_t stop = (stop_fn_t)dlsym(handle, "ddprof_stop_profiling");
  if (stop) {
    stop(1000);
  }

  fprintf(stderr, "PASS: loader constructor succeeded with RTLD_GLOBAL\n");
  dlclose(handle);
  return 0;
}
