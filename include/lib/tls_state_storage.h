// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

// C-compatible constants for the TLS buffer that stores
// TrackerThreadLocalState. Used by loader.c (C) and allocation_tracker.cc
// (C++). Correctness enforced at compile time via static_assert in
// allocation_tracker.cc.
enum {
  DDPROF_TLS_STATE_SIZE = 48,
  DDPROF_TLS_STATE_ALIGN = 8,
};
