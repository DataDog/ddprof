// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "build_id.hpp"

#include <iomanip>

namespace ddprof {

BuildIdStr format_build_id(BuildIdSpan build_id_span) {
  std::string build_id_str;
  // We encode every char on 2 hexa string characters
  build_id_str.resize(build_id_span.size() * 2 + 1);
  for (unsigned i = 0; i < build_id_span.size(); ++i) {
    snprintf(&build_id_str[static_cast<unsigned long>(2 * i)], 3, "%02x",
             build_id_span[i]);
  }
  // remove trailing '\0'
  build_id_str.resize(2 * build_id_span.size());
  return build_id_str;
}

} // namespace ddprof
