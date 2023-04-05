// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

namespace ddprof {
namespace liveallocation {
#ifdef KMAX_TRACKED_ALLOCATIONS
// build time override to reduce execution time of test
static constexpr auto kMaxTracked = KMAX_TRACKED_ALLOCATIONS;
#else
static constexpr auto kMaxTracked = 500000;
#endif
} // namespace liveallocation
} // namespace ddprof
