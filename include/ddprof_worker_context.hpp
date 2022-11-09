// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "live_allocation.hpp"
#include "live_sysallocations.hpp"
#include "file_open_tracker.hpp"
#include "pevent.hpp"
#include "proc_status.hpp"

#include <array>
#include <chrono>

typedef struct DDProfExporter DDProfExporter;
typedef struct DDProfPProf DDProfPProf;
typedef struct PersistentWorkerState PersistentWorkerState;
typedef struct StackHandler StackHandler;
typedef struct UnwindState UnwindState;
typedef struct UserTags UserTags;

// Mutable states within a worker
struct DDProfWorkerContext {
  // Persistent reference to the state shared accross workers
  PersistentWorkerState *persistent_worker_state;
  PEventHdr pevent_hdr;   // perf_event buffer holder
  DDProfExporter *exp[2]; // wrapper around rust exporter
  DDProfPProf *pprof[2];  // wrapper around rust exporter
  int i_current_pprof;
  volatile bool exp_error;
  pthread_t exp_tid;
  UnwindState *us;
  UserTags *user_tags;
  ProcStatus proc_status;
  std::chrono::steady_clock::time_point
      cycle_start_time;  // time at which current export cycle was started
  int64_t send_nanos;    // Last time an export was sent
  uint32_t count_worker; // exports since last cache clear
  std::array<uint64_t, MAX_TYPE_WATCHER> lost_events_per_watcher;
  ddprof::LiveAllocation live_allocation;
  ddprof::SystemAllocation sys_allocation;
  ddprof::FileOpen fileopen;
};
