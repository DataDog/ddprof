// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "allocation_tracker.hpp"

#include <dlfcn.h>
#include <pthread.h>
#include <tuple>

namespace {
using Args = std::tuple<void *(*)(void *), void *>;

void *mystart(void *arg) {
  ddprof::AllocationTracker::notify_thread_start();
  Args *args = reinterpret_cast<Args *>(arg);
  auto [start_routine, start_arg] = *args;
  delete args;
  return start_routine(start_arg);
}

} // namespace

/** Hook pthread_create to cache stack end address just after thread start.
 *
 * The rationale is to fix a deadlock that occurs when user code in created
 * thread calls pthread_getattr:
 * - pthread_getattr takes a lock in pthread object
 * - pthread_getattr itself does an allocation
 * - AllocationTracker tracks the allocation and calls savecontext
 * - savecontext calls pthread_getattr to get stack end address
 * - pthread_getattr is reentered and attempts to take the lock again leading to
 * a deadlock.
 *
 * Workaround is to hook pthread_create and call `cache_stack_end` to
 * cache stack end address while temporarily disabling allocation profiling for
 * current thread before calling user code.
 * */
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg) noexcept {
  static decltype(&::pthread_create) s_pthread_create;
  if (!s_pthread_create) {
    s_pthread_create = reinterpret_cast<decltype(&::pthread_create)>(
        dlsym(RTLD_NEXT, "pthread_create"));
  }
  Args *args = new (std::nothrow) Args{start_routine, arg};
  return args ? s_pthread_create(thread, attr, &mystart, args)
              : s_pthread_create(thread, attr, start_routine, arg);
}
