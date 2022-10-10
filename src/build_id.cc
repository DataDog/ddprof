// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "build_id.hpp"

#include <iomanip>
#include <sstream>

namespace ddprof {

BuildIdStr format_build_id(BuildIdSpan build_id_span) {
  std::stringstream build_id_ss;
  build_id_ss << std::hex;
  std::string dbg_build_id;
  for (auto el : build_id_span) {
    std::stringstream ss;
    ss << std::hex;
    ss << std::setfill('0') << std::setw(2) << static_cast<unsigned int>(el);
    build_id_ss << ss.rdbuf();
  }
  return build_id_ss.str();
}

} // namespace ddprof
