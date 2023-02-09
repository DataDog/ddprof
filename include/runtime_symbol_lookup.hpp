// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"
#include "symbol_map.hpp"
#include "symbol_table.hpp"

#include "ddres_def.hpp"
#include <array>
#include <unordered_map>

namespace ddprof {

class RuntimeSymbolLookup {
public:
  explicit RuntimeSymbolLookup(std::string_view path_to_proc)
      : _path_to_proc(path_to_proc), _cycle_counter(1) {}

  SymbolIdx_t get_or_insert_jitdump(pid_t pid, ProcessAddress_t pc,
                                    SymbolTable &symbol_table,
                                    std::string_view jitdump_path);

  SymbolIdx_t get_or_insert(pid_t pid, ProcessAddress_t pc,
                            SymbolTable &symbol_table);

  void erase(pid_t pid) { _pid_map.erase(pid); }

  void cycle() { ++_cycle_counter; }

private:
  using FailedCycle = std::unordered_map<std::string, uint32_t>;
  struct SymbolInfo {
    SymbolMap _map;
    FailedCycle _failed_cycle;
  };
  using PidUnorderedMap = std::unordered_map<pid_t, SymbolInfo>;

  // Notes on JITDump strategy
  //
  // 1) Retrieve JITDump path
  // Dso type will tell us that there is a JIT file.
  // LLVM sources explain the logic about where we can find it. though we don't
  // need that. The file is mmaped so we can get the path from there.
  //
  // We store in the DSOHdr the fact that we have a JITDump file for the pid.
  //
  // 2) Retrieve symbols
  // Whenever we will come across the symbolisation of an unknown region,
  // we use the runtime_symbol_lookup to check for existing symbols.
  // If none are found, we parse the JITDump file if available.
  // If not, we look for a perf-map file.
  // Symbols are cached with the process's address.
  //
  DDRes fill_from_jitdump(std::string_view jitdump_path, pid_t pid,
                          SymbolMap &symbol_map, SymbolTable &symbol_info);

  DDRes fill_from_perfmap(int pid, SymbolMap &symbol_map,
                          SymbolTable &symbol_table);

  FILE *perfmaps_open(int pid, const char *path_to_perfmap);

  bool has_lookup_failure(const SymbolInfo &symbol_info,
                          std::string_view path) const {
    const auto it = symbol_info._failed_cycle.find(std::string(path));
    if (it != symbol_info._failed_cycle.end()) {
      // failure during this cycle
      return it->second == _cycle_counter;
    }
    return false;
  }

  void flag_lookup_failure(SymbolInfo &symbol_info, std::string_view path) {
    symbol_info._failed_cycle[std::string(path)] = _cycle_counter;
  }

  bool should_skip_symbol(const std::string &symbol);

  bool insert_or_replace(const std::string &symbol, ProcessAddress_t address,
                         Offset_t size, SymbolMap &symbol_map,
                         SymbolTable &symbol_table);

  static const std::array<const char *, 1> _ignored_symbols_start;

  PidUnorderedMap _pid_map;
  std::string _path_to_proc;
  uint32_t _cycle_counter;
};

} // namespace ddprof
