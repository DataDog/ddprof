// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <sys/types.h>

#include "ddprof_defs.hpp"
#include "ddprof_worker_context.hpp"
#include "exporter_input.hpp"
#include "perf_watcher.hpp"

#include <sched.h>
#include <unistd.h>

struct DDProfContext {
  DDProfContext() = default;
  ~DDProfContext() {
    if (params.sockfd != -1) {
      close(params.sockfd);
      params.sockfd = -1;
    }
  }
  struct {
    bool enable{true};
    unsigned upload_period{};
    bool fault_info{true};
    int nice{-1};
    int num_cpu{};
    pid_t pid{0}; // ! only use for perf attach (can be -1 in global mode)
    uint32_t worker_period{}; // exports between worker refreshes
    int dd_profiling_fd{-1};  // opened file descriptor to our internal lib
    int sockfd{-1};
    bool wait_on_socket{false};
    bool show_samples{false};
    cpu_set_t cpu_affinity{};
    std::string switch_user;
    std::string internal_stats;
    std::string tags;
  } params;

  std::vector<PerfWatcher> watchers;
  ExporterInput exp_input;
  DDProfWorkerContext worker_ctx;

private:
  DDProfContext(const DDProfContext &) = delete;
  DDProfContext &operator=(const DDProfContext &) = delete;
  // we could make it moveable (though not needed afaik)
  DDProfContext(DDProfContext &&other) = delete;
  DDProfContext &operator=(const DDProfContext &&) = delete;
};
