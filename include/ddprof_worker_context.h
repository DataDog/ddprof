#pragma once

#include "pevent.h"
#include "proc_status.h"

typedef struct DDProfExporter DDProfExporter;
typedef struct DDProfPProf DDProfPProf;
typedef struct StackHandler StackHandler;
typedef struct StackHandler StackHandler;
typedef struct UnwindState UnwindState;
typedef struct UserTags UserTags;

// Mutable states within a worker
typedef struct DDProfWorkerContext {
  PEventHdr pevent_hdr;   // perf_event buffer holder
  DDProfExporter *exp[2]; // wrapper around rust exporter
  DDProfPProf *pprof[2];  // wrapper around rust exporter
  int i_export;
  volatile bool pending;
  volatile bool exp_error;
  pthread_t exp_tid;
  UnwindState *us;
  UserTags *user_tags;
  ProcStatus proc_status;
  int64_t send_nanos;     // Last time an export was sent
  uint32_t count_worker;  // exports since last cache clear
  uint32_t count_samples; // sample count to avoid bouncing on backpopulates
} DDProfWorkerContext;
