// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#pragma once

#include "datadog/blazesym.h"
#include "ddprof_defs.hpp"
#include "ddres_def.hpp"
#include "map_utils.hpp"
#include "mapinfo_table.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

struct blaze_symbolizer;
struct ddog_prof_Location;

namespace ddprof {
class Symbolizer {

public:
  Symbolizer(bool disable_symbolization);
  ~Symbolizer();

  struct SessionResults {
    std::vector<const blaze_result *> blaze_results{};
  };

  static constexpr int _k_max_stack_depth = kMaxStackDepth;

  DDRes symbolize(const std::span<ElfAddress_t> addrs,
                  const std::string &elf_src, const MapInfo &map_info,
                  std::span<ddog_prof_Location> locations,
                  unsigned &write_index, SessionResults &results);
  static void free_session_results(SessionResults &results) {
    for (auto &result : results.blaze_results) {
      blaze_result_free(result);
      result = nullptr;
    }
  }

  bool is_symbolization_disabled() const { return _disable_symbolization; }

private:
  ddprof::HeterogeneousLookupStringMap<std::string> _demangled_names;
  blaze_symbolizer *_symbolizer;
  bool _disable_symbolization;
};
} // namespace ddprof
