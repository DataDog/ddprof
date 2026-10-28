// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

// Mimics the Go c-shared runtime initialization: the library constructor starts
// a thread that calls pthread_getattr_np and waits for it to finish.
// While the constructor runs, dlopen has not returned yet, so this library's
// GOT is not patched by ddprof: the thread is started without ddprof knowing
// and its pthread_getattr_np call bypasses ddprof's hook.

#include <pthread.h>

namespace {
void *getattr_thread(void * /*arg*/) {
  pthread_attr_t attrs;
  if (pthread_getattr_np(pthread_self(), &attrs) == 0) {
    pthread_attr_destroy(&attrs);
  }
  return nullptr;
}

__attribute__((constructor)) void start_getattr_thread() {
  pthread_t thread;
  if (pthread_create(&thread, nullptr, getattr_thread, nullptr) == 0) {
    pthread_join(thread, nullptr);
  }
}
} // namespace
