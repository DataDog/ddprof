// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <sys/types.h>

#include "ddprof_defs.hpp"
#include "ddprof_worker_context.hpp"
#include "exporter_input.hpp"
#include "perf_clock.hpp"
#include "perf_watcher.hpp"
#include "unique_fd.hpp"

#include <sched.h>
#include <unistd.h>

namespace ddprof {
struct DDProfContext {
  struct {
    bool enable{true};
    std::chrono::seconds upload_period{};
    bool fault_info{true};
    int nice{-1};
    int num_cpu{};
    pid_t pid{0}; // ! only use for perf attach (can be -1 in global mode)
    uint32_t worker_period{}; // exports between worker refreshes
    int dd_profiling_fd{-1};  // opened file descriptor to our internal lib
    std::string socket_path;
    UniqueFd pipefd_to_library;
    bool show_samples{false};
    bool timeline{false};
    cpu_set_t cpu_affinity{};
    std::string switch_user;
    std::string internal_stats;
    std::string tags;
    std::chrono::milliseconds initial_loaded_libs_check_delay{0};
    std::chrono::milliseconds loaded_libs_check_interval{0};
  } params;

  ddprof::UniqueFd socket_fd;
  PerfClockSource perf_clock_source{PerfClockSource::kNoClock};
  std::vector<PerfWatcher> watchers;
  ExporterInput exp_input;
  DDProfWorkerContext worker_ctx;
};
} // namespace ddprof
