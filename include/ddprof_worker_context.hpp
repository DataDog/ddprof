#pragma once

#include "pevent.hpp"
#include "proc_status.hpp"

#include <chrono>

typedef struct DDProfExporter DDProfExporter;
typedef struct DDProfPProf DDProfPProf;
typedef struct PersistentWorkerState PersistentWorkerState;
typedef struct StackHandler StackHandler;
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
};
