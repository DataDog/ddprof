// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

extern "C" {
#include "datadog/common.h"
#include "datadog/profiling.h"
}

#include "defer.hpp"
#include "string_view.hpp"
#include <string_view>

inline ddog_CharSlice to_CharSlice(std::string_view str) {
  return {.ptr = str.data(), .len = str.size()};
}

inline ddog_CharSlice to_CharSlice(string_view slice) {
  return {.ptr = slice.ptr, .len = slice.len};
}

inline void log_warn_and_drop_error(std::string_view message,
                                    ddog_Error *error) {
  defer { ddog_Error_drop(error); };
  ddog_CharSlice char_slice = ddog_Error_message(error);
  LG_WRN("%s (%.*s)", message.data(), (int)char_slice.len, char_slice.ptr);
}
