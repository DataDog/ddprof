// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

// Regression test: a thread unknown to ddprof calls glibc's pthread_getattr_np
// without going through ddprof's hook. glibc calls realloc while holding the
// thread's lock; the allocation hook then lazily initializes the thread state,
// which must not call pthread_getattr_np again (self-deadlock).
// The library under test triggers this from its constructor, the same way the
// Go c-shared runtime does when loaded through dlopen (eg. by a JVM via JNI).

#include <cstdint>
#include <cstdio>
#include <dlfcn.h>

#include "dd_profiling.h"

int main(int argc, char *argv[]) {
  constexpr uint32_t kStopTimeoutMs = 1000;
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <library>\n", argv[0]);
    return 1;
  }

  int const ret = ddprof_start_profiling();
  if (ret != 0) {
    fprintf(stderr, "Failed to start profiling (ret=%d)\n", ret);
    return 1;
  }

  void *handle = dlopen(argv[1], RTLD_NOW);
  if (!handle) {
    fprintf(stderr, "dlopen failed: %s\n", dlerror());
    return 1;
  }
  dlclose(handle);

  ddprof_stop_profiling(kStopTimeoutMs);
  return 0;
}
