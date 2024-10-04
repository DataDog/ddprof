// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "base_frame_symbol_lookup.hpp"

#include "dso_type.hpp"
#include "logger.hpp"

#include <absl/strings/str_cat.h>
#include <filesystem>

namespace ddprof {

namespace {
Symbol symbol_from_pid(pid_t pid) {
  return {{}, {}, 0, absl::StrCat("pid_", pid)};
}
} // namespace

SymbolIdx_t
BaseFrameSymbolLookup::insert_bin_symbol(pid_t pid, SymbolTable &symbol_table,
                                         DsoSymbolLookup &dso_symbol_lookup,
                                         DsoHdr &dso_hdr) {
  SymbolIdx_t symbol_idx = -1;

  DsoHdr::DsoFindRes const find_res =
      dso_hdr.dso_find_first_std_executable(pid);
  if (find_res.second && has_relevant_path(find_res.first->second._type)) {
    // todo : how to tie lifetime of DSO to this ?
    symbol_idx =
        dso_symbol_lookup.get_or_insert(find_res.first->second, symbol_table);
    _bin_map.insert({pid, symbol_idx});
    const std::filesystem::path path(find_res.first->second._filename);
    const std::string base_name = path.filename().string();
    _exe_name_map.insert({pid, base_name});
  } else {
    std::string exe_name;
    bool const exe_found = dso_hdr.find_exe_name(pid, exe_name);
    if (exe_found) {
      const std::filesystem::path path(exe_name);
      const std::string base_name = path.filename().string();
      symbol_idx = symbol_table.size();
      symbol_table.emplace_back(Symbol({}, base_name, 0, exe_name));
      _bin_map.insert({pid, symbol_idx});
      _exe_name_map.insert({pid, base_name});
    }
  }
  return symbol_idx;
}

SymbolIdx_t
BaseFrameSymbolLookup::get_or_insert(pid_t pid, SymbolTable &symbol_table,
                                     DsoSymbolLookup &dso_symbol_lookup,
                                     DsoHdr &dso_hdr) {
  auto const it_bin = _bin_map.find(pid);
  auto const it_pid = _pid_map.find(pid);

  SymbolIdx_t symbol_idx = -1;
  if (it_bin != _bin_map.end()) {
    symbol_idx = it_bin->second;
  } else {
    // attempt k nb times to look for binary info
    if (it_pid == _pid_map.end() ||
        ++it_pid->second._nb_bin_lookups < k_nb_bin_lookups) {
      symbol_idx =
          insert_bin_symbol(pid, symbol_table, dso_symbol_lookup, dso_hdr);
    }
  }
  if (symbol_idx == -1 && it_pid != _pid_map.end()) {
    // We already build a pid symbol for this pid
    symbol_idx = it_pid->second._symb_idx;
  }
  // First time we fail on this pid : insert a pid info in symbol table
  if (symbol_idx == -1) {
    symbol_idx = symbol_table.size();
    symbol_table.push_back(symbol_from_pid(pid));
    _pid_map.emplace(pid, PidSymbol(symbol_idx));
  }
  return symbol_idx;
}

std::string_view BaseFrameSymbolLookup::get_exe_name(pid_t pid) const {
  auto const it = _exe_name_map.find(pid);
  if (it != _exe_name_map.end()) {
    return it->second;
  }
  return {};
}

} // namespace ddprof
