// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "live_allocation.hpp"
#include "pevent.hpp"
#include "proc_status.hpp"
#include "bpf/sample_processor.h"

#include <array>
#include <chrono>

#include <array>
#include <atomic>

template<typename T, size_t Size>
class SimpleRingBuffer {
public:
  SimpleRingBuffer() : head(0), tail(0) {}

  bool try_push(const T& value) {
    size_t current_head = head.load(std::memory_order_relaxed);
    size_t next_head = nextIndex(current_head);
    if (next_head == tail.load(std::memory_order_acquire)) {
      return false; // Buffer is full
    }
    buffer[current_head] = value;
    head.store(next_head, std::memory_order_release);
    return true;
  }

  bool pop(T& value) {
    size_t current_tail = tail.load(std::memory_order_relaxed);
    if (current_tail == head.load(std::memory_order_acquire)) {
      return false; // Buffer is empty
    }
    value = buffer[current_tail];
    tail.store(nextIndex(current_tail), std::memory_order_release);
    return true;
  }

  bool empty() const {
    return head.load(std::memory_order_acquire) == tail.load(std::memory_order_relaxed);
  }

private:
  size_t nextIndex(size_t index) const {
    return (index + 1) % Size;
  }

  std::array<T, Size> buffer;
  std::atomic<size_t> head;
  std::atomic<size_t> tail;
};


namespace ddprof {

struct DDProfExporter;
struct DDProfPProf;
struct PersistentWorkerState;
struct StackHandler;
struct UnwindState;
struct UserTags;

struct BPFEvents{
  std::atomic<bool> keep_running{true};
  SimpleRingBuffer<stacktrace_event, 1000> _events;
};

// Mutable states within a worker
struct DDProfWorkerContext {
  // Persistent reference to the state shared accross workers
  PersistentWorkerState *persistent_worker_state{nullptr};
  PEventHdr pevent_hdr;     // perf_event buffer holder
  DDProfExporter *exp[2]{}; // wrapper around rust exporter
  DDProfPProf *pprof[2]{};  // wrapper around rust exporter
  int i_current_pprof{0};
  volatile bool exp_error{false};
  pthread_t exp_tid{0};
  UnwindState *us{};

  BPFEvents _bpf_events;

  UserTags *user_tags{};
  ProcStatus proc_status{};
  std::chrono::steady_clock::time_point
      cycle_start_time{}; // time at which current export cycle was started
  std::chrono::steady_clock::time_point
      send_time{};          // Last time an export was sent
  uint32_t count_worker{0}; // exports since last cache clear
  std::array<uint64_t, kMaxTypeWatcher> lost_events_per_watcher{};
  LiveAllocation live_allocation;
  int64_t perfclock_offset;
};

} // namespace ddprof
