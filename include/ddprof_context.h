// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <sys/types.h>

#include "ddprof_defs.h"
#include "exporter_input.h"
#include "perf_option.h"
#include "pevent.h"
#include "proc_status.h"

// forward declarations
typedef struct DDProfExporter DDProfExporter;
typedef struct DDProfPProf DDProfPProf;
typedef struct StackHandler StackHandler;
typedef struct PEventHdr PEventHdr;
typedef struct StackHandler StackHandler;
typedef struct UnwindState UnwindState;
typedef struct UserTags UserTags;

// Mutable states within a worker
typedef struct DDProfWorkerContext {
  PEventHdr pevent_hdr; // perf_event buffer holder
  DDProfExporter *exp;  // wrapper around rust exporter
  DDProfPProf *pprof;   // wrapper around rust exporter
  UnwindState *us;
  UserTags *user_tags;
  ProcStatus proc_status;
  int64_t send_nanos;     // Last time an export was sent
  uint32_t count_worker;  // exports since last cache clear
  uint32_t count_samples; // sample count to avoid bouncing on backpopulates
} DDProfWorkerContext;

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
  void *callback_ctx; // user state to be used in callback
  DDProfWorkerContext worker_ctx;
} DDProfContext;
