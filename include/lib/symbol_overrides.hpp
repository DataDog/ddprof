// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <chrono>

namespace ddprof {
enum class OverrideMode { kGOTOverride, kTrampoline };

void setup_overrides(OverrideMode mode);
void restore_overrides();

// check if new libs have been loaded and update overrides accordingly
void update_overrides();

} // namespace ddprof
