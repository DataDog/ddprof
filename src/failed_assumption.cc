// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "failed_assumption.hpp"

#include "logger.hpp"

namespace ddprof {
void report_failed_assumption(std::string_view sv) {
  LG_WRN("Failed assumption: %.*s", static_cast<int>(sv.size()), sv.data());
}
} // namespace ddprof