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

// Mutable states within a worker
typedef struct DDProfWorkerContext {
  PEventHdr pevent_hdr; // perf_event buffer holder
  DDProfExporter *exp;  // wrapper around rust exporter
  DDProfPProf *pprof;   // wrapper around rust exporter
  UnwindState *us;
  ProcStatus proc_status;
  int64_t send_nanos;    // Last time an export was sent
  uint32_t count_worker; // exports since last cache clear
  uint32_t count_cache;  // exports since worker was spawned
} DDProfWorkerContext;

typedef struct DDProfContext {
  struct {
    bool enable;
    double upload_period;
    bool faultinfo;
    bool coredumps;
    int nice;
    bool sendfinal;
    pid_t pid; // ! only use for perf attach (can be -1 in global mode)
    bool global;
    uint32_t worker_period; // exports between worker refreshes
    uint32_t cache_period;  // exports between cache clears
    const char *internalstats;
  } params;

  ExporterInput exp_input;
  const StackHandler *stack_handler;
  PerfOption watchers[MAX_TYPE_WATCHER];
  int num_watchers;
  void *callback_ctx; // user state to be used in callback
  DDProfWorkerContext worker_ctx;
} DDProfContext;
