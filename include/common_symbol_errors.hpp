// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <array>
#include <string_view>

// sv operator
using namespace std::string_view_literals;

namespace ddprof {

inline constexpr std::array<std::string_view, 6> k_common_frame_names = {
    "[truncated]"sv,      "[unknown mapping]"sv,
    "[unwind failure]"sv, "[incomplete]"sv,
    "[lost]"sv,           "[maximum pids]"sv};

enum SymbolErrors {
  truncated_stack = 0,
  unknown_mapping,
  unwind_failure,
  incomplete_stack,
  lost_event,
  max_pids,
};

} // namespace ddprof
