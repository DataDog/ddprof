// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

// Workers are reset by creating new forks. This structure is shared accross
// processes
typedef struct PersistentWorkerState {
  volatile bool restart_worker;
  volatile bool errors;
  // Number of sequences since the beginning of the app / profiling
  // Why not volatile ? Although several threads can update the number of
  // cycles, by design Only a single thread reads and writes to this variable.
  uint32_t profile_seq;
} PersistentWorkerState;
