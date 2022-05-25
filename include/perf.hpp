// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <linux/perf_event.h>
#include <vector>

namespace ddprof {
std::vector<perf_event_attr>
all_perf_configs_from_watcher(const PerfWatcher *watcher, bool extras);
}
