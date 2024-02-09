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
  Symbolizer();
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

private:
  ddprof::HeterogeneousLookupStringMap<std::string> _demangled_names;
  blaze_symbolizer *_symbolizer;
};
} // namespace ddprof
