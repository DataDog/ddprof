// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dso_symbol_lookup.hpp"

#include "ddog_profiling_utils.hpp"
#include "ddprof_file_info-i.hpp"
#include "dso_type.hpp"
#include "logger.hpp"

#include <absl/strings/str_format.h>
#include <algorithm>
#include <filesystem>

namespace ddprof {

namespace {

Symbol symbol_from_unhandled_dso(const Dso &dso,
                                 const ddog_prof_ProfilesDictionary *dict) {
  return make_symbol(std::string(), std::string(), 0, dso_type_str(dso._type),
                     dict);
}

Symbol symbol_from_dso(ElfAddress_t normalized_addr, const Dso &dso,
                       std::string_view addr_type,
                       const ddog_prof_ProfilesDictionary *dict) {
  // address that means something for our user (addr)
  std::string const dso_dbg_str = normalized_addr
      ? absl::StrFormat("[%#x:%s]", normalized_addr, addr_type)
      : std::filesystem::path(dso.format_filename()).filename().string();
  return make_symbol(dso_dbg_str, dso_dbg_str, 0, dso.format_filename(), dict);
}
} // namespace

SymbolIdx_t DsoSymbolLookup::get_or_insert_unhandled_type(
    const Dso &dso, SymbolTable &symbol_table,
    const ddog_prof_ProfilesDictionary *dict) {
  auto const it = _map_unhandled_dso.find(dso._type);
  SymbolIdx_t symbol_idx;

  if (it != _map_unhandled_dso.end()) {
    symbol_idx = it->second;
  } else {
    symbol_idx = symbol_table.size();
    symbol_table.push_back(symbol_from_unhandled_dso(dso, dict));
    _map_unhandled_dso.insert({dso._type, symbol_idx});
  }
  return symbol_idx;
}

SymbolIdx_t DsoSymbolLookup::get_or_insert(
    FileAddress_t normalized_addr, const Dso &dso, SymbolTable &symbol_table,
    const ddog_prof_ProfilesDictionary *dict, std::string_view addr_type) {
  // Only add address information for relevant dso types
  if (!has_relevant_path(dso._type) && dso._type != DsoType::kVdso &&
      dso._type != DsoType::kVsysCall) {
    return get_or_insert_unhandled_type(dso, symbol_table, dict);
  }
  // Note: using file ID could be more generic
  AddressMap &addr_lookup = _map_dso_path[dso._filename];
  auto const it = addr_lookup.find(normalized_addr);
  SymbolIdx_t symbol_idx;
  if (it != addr_lookup.end()) {
    symbol_idx = it->second;
  } else { // insert things
    symbol_idx = symbol_table.size();
    symbol_table.push_back(
        symbol_from_dso(normalized_addr, dso, addr_type, dict));
    addr_lookup.insert({normalized_addr, symbol_idx});
  }
  return symbol_idx;
}

SymbolIdx_t
DsoSymbolLookup::get_or_insert(const Dso &dso, SymbolTable &symbol_table,
                               const ddog_prof_ProfilesDictionary *dict) {
  return get_or_insert(0, dso, symbol_table, dict);
}

void DsoSymbolLookup::stats_display() const {
  LG_NTC("DSO_SYMB  | %10s | %lu", "SIZE", get_size());
}

size_t DsoSymbolLookup::get_size() const {
  unsigned total_nb_elts = 0;
  std::for_each(_map_dso_path.begin(), _map_dso_path.end(),
                [&](DsoPathMap::value_type const &el) {
                  total_nb_elts += el.second.size();
                });
  return total_nb_elts;
}

} // namespace ddprof
