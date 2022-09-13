// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "build_id.hpp"

#include <sstream>

namespace ddprof {

BuildIdStr format_build_id(BuildIdSpan build_id_span) {
  std::stringstream ss;
  ss << std::hex;
  std::string dbg_build_id;
  for (auto el : build_id_span) {
    ss << static_cast<unsigned int>(el);
  }
  return ss.str();
}

}
