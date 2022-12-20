// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once
#include <atomic>
#include <chrono>
#include <iostream>
#include <new>
#include <thread>

#include <sys/mman.h>

template <class T> class AtomicShared : public std::atomic<T> {
public:
  static void *operator new(size_t) {
    void *const pv = mmap(0, sizeof(T), PROT_READ | PROT_WRITE,
                          MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (pv == MAP_FAILED)
      throw std::bad_alloc();
    return pv;
  };
  static void operator delete(void *pv) { munmap(pv, sizeof(T)); };
  AtomicShared &operator=(const int b) {
    std::atomic<pid_t>::operator=(b);
    return *this;
  }

  bool value_timedwait(const T &oldval, int timeout) {
    // Block until the value is different from oldval.  If the timeout is 0,
    // check once without blocking.  If the value is negative, then block
    // indefinitely (may be "expensive" in some sense).
    // Doesn't do anything fancy to enforce re-scheduling the thread when
    // the condition occurs, nor to decrease sleep overhead.  As per the spec,
    // doesn't protect against the ABA problem (A changes to B, then back to A,
    // before B can be detected in the loop).
    // Will perform three "fast checks" before starting to yield to the
    // scheduler.  This will appear as a hotspot when the caller has to wait
    // a lot.
    auto start = std::chrono::high_resolution_clock::now();
    auto end = std::chrono::milliseconds(timeout);
    if (timeout < 0) {
      end = std::chrono::hours::max();
    }
    int fast_checks = 3; // hardcoded
    do {
      if (this->load() != oldval) {
        return true;
      } else if (fast_checks > 0) {
        --fast_checks;
      } else {
        std::this_thread::yield();
      }
    } while (std::chrono::high_resolution_clock::now() - start < end);
    return false;
  }
};
