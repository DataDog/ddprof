// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#pragma once

#include "datadog/blazesym.h"
#include "ddprof_defs.hpp"
#include "ddprof_file_info-i.hpp"
#include "ddres_def.hpp"
#include "map_utils.hpp"
#include "mapinfo_table.hpp"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct blaze_symbolizer;
struct ddog_prof_Location;

namespace ddprof {
class Symbolizer {
public:
  explicit Symbolizer(bool disable_symbolization = false)
      : _disable_symbolization(disable_symbolization) {}

  struct SessionResults {
    std::vector<const blaze_result *> blaze_results{};
  };

  static constexpr int _k_max_stack_depth = kMaxStackDepth;

  DDRes symbolize_pprof(std::span<ElfAddress_t> addrs, FileInfoId_t file_id,
                        const std::string &elf_src, const MapInfo &map_info,
                        std::span<ddog_prof_Location> locations,
                        unsigned &write_index, SessionResults &results);
  static void free_session_results(SessionResults &results) {
    for (auto &result : results.blaze_results) {
      blaze_result_free(result);
      result = nullptr;
    }
  }
  int clear_unvisited();
  void mark_unvisited();

private:
  struct SymbolizerWrapper {
    constexpr static blaze_symbolizer_opts opts{
        .type_size = sizeof(blaze_symbolizer_opts),
        .auto_reload = false,
        .code_info = true,
        .inlined_fns = false,
        .demangle = false,
        .reserved = {}};

    explicit SymbolizerWrapper(std::string elf_src)
        : _symbolizer(std::unique_ptr<blaze_symbolizer,
                                      decltype(&blaze_symbolizer_free)>(
              blaze_symbolizer_new_opts(&opts), &blaze_symbolizer_free)),
          _elf_src(std::move(elf_src)) {}

    std::unique_ptr<blaze_symbolizer, decltype(&blaze_symbolizer_free)>
        _symbolizer;
    ddprof::HeterogeneousLookupStringMap<std::string> _demangled_names;
    std::string _elf_src;
    bool _visited{true};
  };

  std::unordered_map<FileInfoId_t, SymbolizerWrapper> _symbolizer_map;
  bool _disable_symbolization;
};
} // namespace ddprof