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
#include <utility>

// Forward declarations for libdatadog types (must be at global scope)
struct ddog_prof_ProfilesDictionary;
using ddog_prof_ProfilesDictionaryHandle = ddog_prof_ProfilesDictionary *;

namespace ddprof {

// RAII wrapper around a libdatadog ProfilesDictionary handle.
// The libdatadog typedef `ProfilesDictionaryHandle` is already a pointer
// (`ProfilesDictionary *`), and the C API takes it by pointer-to-handle so
// it can write/zero the slot. We hold the handle inline — callers that need
// the pointer-to-handle the C API wants just call `get()`.
class ProfilesDictionary {
public:
  ProfilesDictionary();
  ~ProfilesDictionary();

  ProfilesDictionary(const ProfilesDictionary &) = delete;
  ProfilesDictionary &operator=(const ProfilesDictionary &) = delete;

  ProfilesDictionary(ProfilesDictionary &&o) noexcept
      : _handle(std::exchange(o._handle, nullptr)) {}
  ProfilesDictionary &operator=(ProfilesDictionary &&o) noexcept {
    if (this != &o) {
      reset();
      _handle = std::exchange(o._handle, nullptr);
    }
    return *this;
  }

  explicit operator bool() const { return _handle != nullptr; }

  // Returns a pointer to the handle, as required by
  // ddog_prof_Profile_with_dictionary (which takes `const
  // ProfilesDictionaryHandle *`). The API is const — it does not zero or
  // replace the slot.
  [[nodiscard]] const ddog_prof_ProfilesDictionaryHandle *get() const {
    return &_handle;
  }
  [[nodiscard]] const ddog_prof_ProfilesDictionary *dict() const {
    return _handle;
  }

private:
  void reset();

  ddog_prof_ProfilesDictionaryHandle _handle{};
};

struct SymbolHdr {
  explicit SymbolHdr(std::string_view path_to_proc = "");
  ~SymbolHdr() = default;

  SymbolHdr(const SymbolHdr &) = delete;
  SymbolHdr &operator=(const SymbolHdr &) = delete;
  SymbolHdr(SymbolHdr &&) noexcept = default;
  SymbolHdr &operator=(SymbolHdr &&) noexcept = default;
  void display_stats() const { _dso_symbol_lookup.stats_display(); }
  void cycle() { _runtime_symbol_lookup.cycle(); }

  [[nodiscard]] const ddog_prof_ProfilesDictionary *
  profiles_dictionary() const {
    return _profiles_dictionary.dict();
  }

  void clear(pid_t pid) {
    _base_frame_symbol_lookup.erase(pid);
    // mappings are only relevant in the context of a given pid.
    _mapinfo_lookup.erase(pid);
    _runtime_symbol_lookup.erase(pid);
  }

  // String interning dictionary (persists across profile exports).
  // MUST be declared first so it is destroyed last — Symbol and MapInfoTable
  // entries hold pointers into this dictionary.
  ProfilesDictionary _profiles_dictionary;

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
