// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <sched.h>
#include <string>

namespace ddprof {
bool parse_cpu_mask(std::string_view sv, cpu_set_t &cpu_mask);

std::string cpu_mask_to_string(const cpu_set_t &cpu_mask);

// Return number of configured processors
int nprocessors_conf();

} // namespace ddprof