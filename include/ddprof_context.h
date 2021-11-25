// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <sys/types.h>

#include "ddprof_defs.h"
#include "ddprof_worker_context.h"
#include "exporter_input.h"
#include "perf_option.h"

// forward declarations
typedef struct StackHandler StackHandler;

typedef struct DDProfContext {
  struct {
    bool enable;
    double upload_period;
    bool faultinfo;
    bool coredumps;
    int nice;
    int num_cpu;
    bool sendfinal;
    pid_t pid; // ! only use for perf attach (can be -1 in global mode)
    bool global;
    uint32_t worker_period; // exports between worker refreshes
    const char *internalstats;
    const char *tags;
  } params;

  ExporterInput exp_input;
  const StackHandler *stack_handler;
  PerfOption watchers[MAX_TYPE_WATCHER];
  int num_watchers;
  void *callback_ctx; // user state to be used in callback (lib mode)
  DDProfWorkerContext worker_ctx;
} DDProfContext;
