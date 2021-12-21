// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dso_symbol_lookup.hpp"

#include "dso_type.hpp"
#include "string_format.hpp"

extern "C" {
#include "logger.h"
}

#include <algorithm>

namespace ddprof {

namespace {

Symbol symbol_from_unhandled_dso(const Dso &dso) {
  return Symbol(dso._pgoff, std::string(), std::string(), 0,
                dso::dso_type_str(dso._type));
}

Symbol symbol_from_dso(ElfAddress_t normalized_addr, const Dso &dso) {
  // address that means something for our user (addr)
  std::string dso_dbg_str =
      normalized_addr ? string_format("[%p:file]", normalized_addr) : "";
  return Symbol(dso._pgoff, dso_dbg_str, dso_dbg_str, 0, dso.format_filename());
}
} // namespace

SymbolIdx_t
DsoSymbolLookup::get_or_insert_unhandled_type(const Dso &dso,
                                              SymbolTable &symbol_table) {
  auto const it = _map_unhandled_dso.find(dso._type);
  SymbolIdx_t symbol_idx;

  if (it != _map_unhandled_dso.end()) {
    symbol_idx = it->second;
  } else {
    symbol_idx = symbol_table.size();
    symbol_table.push_back(symbol_from_unhandled_dso(dso));
    _map_unhandled_dso.insert(
        std::pair<dso::DsoType, SymbolIdx_t>(dso._type, symbol_idx));
  }
  return symbol_idx;
}

SymbolIdx_t DsoSymbolLookup::get_or_insert(ElfAddress_t addr, const Dso &dso,
                                           SymbolTable &symbol_table) {
  // Only add address information for relevant dso types
  if (dso._type != dso::kStandard && dso._type != dso::kVdso &&
      dso._type != dso::kVsysCall) {
    return get_or_insert_unhandled_type(dso, symbol_table);
  }
  AddressMap &addr_lookup = _map_dso_path[dso._filename];
  FileAddress_t normalized_addr = (addr - dso._start) + dso._pgoff;

  auto const it = addr_lookup.find(normalized_addr);
  SymbolIdx_t symbol_idx;
  if (it != addr_lookup.end()) {
    symbol_idx = it->second;
  } else { // insert things
    symbol_idx = symbol_table.size();
    symbol_table.push_back(symbol_from_dso(normalized_addr, dso));
    addr_lookup.insert(
        std::pair<FileAddress_t, SymbolIdx_t>(normalized_addr, symbol_idx));
  }
  return symbol_idx;
}

SymbolIdx_t DsoSymbolLookup::get_or_insert(const Dso &dso,
                                           SymbolTable &symbol_table) {
  // use start an address that will be zeroed as a trick to remove addr
  // info (as we simply want the binary info)
  return get_or_insert(dso._start - dso._pgoff, dso, symbol_table);
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
