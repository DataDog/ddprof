// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <chrono>
#include <linux/perf_event.h>

#include "ddres.hpp"
#include "persistent_worker_state.hpp"
#include "pevent.hpp"

namespace ddprof {
struct DDProfContext;

DDRes ddprof_worker_init(DDProfContext &ctx,
                         PersistentWorkerState *persistent_worker_state);
DDRes ddprof_worker_free(DDProfContext &ctx);
DDRes ddprof_worker_maybe_export(DDProfContext &ctx,
                                 std::chrono::steady_clock::time_point now_ns);
DDRes ddprof_worker_cycle(DDProfContext &ctx,
                          std::chrono::steady_clock::time_point now,
                          bool synchronous_export);
DDRes ddprof_worker_process_event(const perf_event_header *hdr, int watcher_pos,
                                  DDProfContext &ctx);

// Only init unwinding elements
DDRes worker_library_init(DDProfContext &ctx,
                          PersistentWorkerState *persistent_worker_state);
DDRes worker_library_free(DDProfContext &ctx);
} // namespace ddprof
