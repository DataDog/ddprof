// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

// C++ wrapper to expose AllocationTracker functions to C test

#include "lib/allocation_tracker.hpp"

extern "C" {

void *AllocationTracker_get_tl_state(void) {
  return ddprof::AllocationTracker::get_tl_state();
}

void *AllocationTracker_init_tl_state(void) {
  return ddprof::AllocationTracker::init_tl_state();
}
}
