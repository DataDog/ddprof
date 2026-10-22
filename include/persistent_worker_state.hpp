// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

namespace ddprof {
// Workers are reset by creating new forks. This structure is shared accross
// processes
struct PersistentWorkerState {
  volatile bool restart_worker;
  volatile bool errors;
  // Number of sequences since the beginning of the app / profiling
  // Why not volatile ? Although several threads can update the number of
  // cycles, by design Only a single thread reads and writes to this variable.
  uint32_t profile_seq;
  // memfd holding the most recent serialized live-allocation snapshot.
  // The fd is opened by the parent in main_loop and inherited by every
  // worker child. A child that is restarting writes its serialized
  // LiveAllocation state to this fd just before exiting; the next child
  // reads it during worker_library_init so live-heap tracking survives
  // worker resets. -1 if snapshotting is disabled.
  int live_alloc_snapshot_fd;
};

} // namespace ddprof
