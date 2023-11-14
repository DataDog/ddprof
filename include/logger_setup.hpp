// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <cstdint>

namespace ddprof {
inline constexpr auto kMaxLogPerSecForNonDebug = 100;

void setup_logger(
    const char *log_mode, const char *log_level,
    uint64_t max_log_per_sec_for_non_debug = kMaxLogPerSecForNonDebug);

} // namespace ddprof
