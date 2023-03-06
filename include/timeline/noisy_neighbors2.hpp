// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <unordered_map>
#include <string>
#include <vector>

#include <linux/sched.h>

#include "perf.hpp"
#include "logger.hpp"

struct ThreadState {
  unsigned short cpu;
  pid_t pid = -1;
  std::string comm;
  uint64_t begin;
  uint64_t end;
  bool in_syscall;
  int syscall_number;
  long state;
};

struct NoisyNeighbors {
  std::vector<ThreadState> cpu_on;
  std::unordered_map<pid_t, ThreadState> cpu_off;
  std::vector<std::vector<ThreadStates>> completed_states; // Per-CPU buffer of completed states

  // Functions
  NoisyNeighbors(int num_cpu); 
  process_event(perf_event_sample *sample, const std::string &str);
  nlohmann::json finalize(uint64_t last_time);
  void clear();

private:
  void sched_switch(perf_event_sample *sample);
  void sched_migrate(perf_event_sample *sample);
  void sched_runtime(perf_event_sample *sample);
  void syscall_enter(perf_event_sample *sample);
  void syscall_exit(perf_event_sample *sample);
  uint64_t base_ns;
  uint64_t set_uptime_ns();
};
