// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <array>
#include <atomic>
#include <thread>

namespace ddprof {

class ThreadEntries {
public:
  static constexpr size_t max_threads = 10;
  ThreadEntries() noexcept { reset(); }
  void reset() noexcept {
    for (auto &entry : _thread_entries) {
      entry.store(-1, std::memory_order_relaxed);
    }
  }

  std::atomic<pid_t> &get_entry(size_t idx) noexcept {
    return _thread_entries[idx];
  }

private:
  std::array<std::atomic<pid_t>, max_threads> _thread_entries;
};

class TLReentryGuard {
public:
  explicit TLReentryGuard(ThreadEntries &entries, pid_t tid)
      : _entries(entries) {
    while (true) {
      for (size_t i = 0; i < ThreadEntries::max_threads; ++i) {
        pid_t expected = -1;
        if (_entries.get_entry(i).compare_exchange_weak(
                expected, tid, std::memory_order_acq_rel)) {
          _ok = true;
          _index = i;
          return;
        }
        if (expected == tid) {
          // This thread is already in the entries.
          return;
        }
      }
      // If we've reached here, all slots are occupied and none of them belongs
      // to this thread. Let's yield to other threads and then try again.
      std::this_thread::yield();
    }
  }

  ~TLReentryGuard() {
    if (_ok) {
      _entries.get_entry(_index).store(-1, std::memory_order_release);
    }
  }

  explicit operator bool() const { return _ok; }

  TLReentryGuard(const TLReentryGuard &) = delete;
  TLReentryGuard &operator=(const TLReentryGuard &) = delete;

private:
  ThreadEntries &_entries;
  bool _ok{false};
  int _index{-1};
};

class ReentryGuard {
public:
  explicit ReentryGuard(bool *reentry_guard) : _reentry_guard(reentry_guard) {
    if (_reentry_guard) {
      _ok = (!*_reentry_guard);
      *_reentry_guard = true;
    }
  }

  ~ReentryGuard() {
    if (_ok) {
      *_reentry_guard = false;
    }
  }

  explicit operator bool() const { return _ok; }

  ReentryGuard(const ReentryGuard &) = delete;
  ReentryGuard &operator=(const ReentryGuard &) = delete;

private:
  bool *_reentry_guard;
  bool _ok{false};
};

} // namespace ddprof
