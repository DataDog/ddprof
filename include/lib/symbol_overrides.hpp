// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

namespace ddprof {
enum class OverrideMode { kGOTOverride, kTrampoline };

void setup_overrides(OverrideMode mode);
void restore_overrides(OverrideMode mode);
void reinstall_timer_after_fork();
} // namespace ddprof