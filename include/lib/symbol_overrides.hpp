// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <chrono>

namespace ddprof {
void setup_overrides(std::chrono::milliseconds initial_loaded_libs_check_delay,
                     std::chrono::milliseconds loaded_libs_check_interval);
void restore_overrides();
void reinstall_timer_after_fork();
} // namespace ddprof