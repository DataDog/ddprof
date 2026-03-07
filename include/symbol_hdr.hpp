// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "base_frame_symbol_lookup.hpp"
#include "common_mapinfo_lookup.hpp"
#include "common_symbol_lookup.hpp"
#include "ddres_def.hpp"
#include "dso_symbol_lookup.hpp"
#include "logger.hpp"
#include "mapinfo_lookup.hpp"
#include "runtime_symbol_lookup.hpp"

#include <cstdlib>
#include <memory>

// Forward declarations for libdatadog types (must be at global scope)
struct ddog_prof_ProfilesDictionary;
using ddog_prof_ProfilesDictionaryHandle = ddog_prof_ProfilesDictionary *;

namespace ddprof {

struct ProfilesDictionaryDeleter {
  void operator()(ddog_prof_ProfilesDictionaryHandle *handle) const;
};

using ProfilesDictionaryPtr =
    std::unique_ptr<ddog_prof_ProfilesDictionaryHandle,
                    ProfilesDictionaryDeleter>;

struct SymbolHdr {
  explicit SymbolHdr(std::string_view path_to_proc = "");
  ~SymbolHdr() = default;

  SymbolHdr(const SymbolHdr &) = delete;
  SymbolHdr &operator=(const SymbolHdr &) = delete;
  SymbolHdr(SymbolHdr &&) noexcept = default;
  SymbolHdr &operator=(SymbolHdr &&) noexcept = default;
  void display_stats() const { _dso_symbol_lookup.stats_display(); }
  void cycle() { _runtime_symbol_lookup.cycle(); }

  const ddog_prof_ProfilesDictionary *profiles_dictionary() const {
    return _profiles_dictionary ? *_profiles_dictionary : nullptr;
  }

  void clear(pid_t pid) {
    _base_frame_symbol_lookup.erase(pid);
    // mappings are only relevant in the context of a given pid.
    _mapinfo_lookup.erase(pid);
    _runtime_symbol_lookup.erase(pid);
  }

  // String interning dictionary (persists across profile exports)
  // MUST be declared first so it is destroyed last - Symbol and MapInfo
  // objects store pointers into this dictionary.
  ProfilesDictionaryPtr _profiles_dictionary;

  // Cache symbol associations
  BaseFrameSymbolLookup _base_frame_symbol_lookup;
  CommonSymbolLookup _common_symbol_lookup;
  DsoSymbolLookup _dso_symbol_lookup;
  RuntimeSymbolLookup _runtime_symbol_lookup;
  // Symbol table (contains the references to strings)
  SymbolTable _symbol_table;

  // Cache mapping associations
  CommonMapInfoLookup _common_mapinfo_lookup;
  MapInfoLookup _mapinfo_lookup;

  // The mapping table
  MapInfoTable _mapinfo_table;
};

} // namespace ddprof
