// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <sys/types.h>

#include "ddprof_defs.hpp"
#include "ddprof_worker_context.hpp"
#include "exporter_input.hpp"
#include "logger.hpp"
#include "metric_aggregator.hpp"
#include "perf_watcher.hpp"

#include <sched.h>
#include <string>

// forward declarations
typedef struct StackHandler StackHandler;

typedef struct DDProfContext {
  struct {
    bool enable;
    double upload_period;
    bool fault_info;
    bool core_dumps;
    int nice;
    int num_cpu;
    pid_t pid; // ! only use for perf attach (can be -1 in global mode)
    bool global;
    uint32_t worker_period; // exports between worker refreshes
    int dd_profiling_fd;    // opened file descriptor to our internal lib
    int sockfd;
    bool wait_on_socket;
    bool show_samples;
    cpu_set_t cpu_affinity;
    std::string switch_user;
    std::string internal_stats;
    std::string tags;
  } params;

  bool initialized;
  ExporterInput exp_input;
  const StackHandler *stack_handler;
  PerfWatcher watchers[MAX_TYPE_WATCHER];
  int num_watchers;
  void *callback_ctx; // user state to be used in callback (lib mode)
  DDProfWorkerContext worker_ctx;
  MetricAggregator metrics;

  void release() noexcept;
} DDProfContext;
