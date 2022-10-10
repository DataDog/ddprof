// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"
#include "symbol_map.hpp"
#include "symbol_table.hpp"

#include <unordered_map>

namespace ddprof {

class RuntimeSymbolLookup {
public:
  explicit RuntimeSymbolLookup(std::string_view path_to_proc)
      : _path_to_proc(path_to_proc) {}
  SymbolIdx_t get_or_insert(pid_t pid, ProcessAddress_t pc,
                            SymbolTable &symbol_table);
  void erase(pid_t pid) { _pid_map.erase(pid); }

private:
  using PidUnorderedMap = std::unordered_map<pid_t, SymbolMap>;
  void fill_perfmap_from_file(int pid, SymbolMap &symbol_map,
                              SymbolTable &symbol_table);
  FILE *perfmaps_open(int pid, const char *path_to_perfmap);

  PidUnorderedMap _pid_map;
  std::string _path_to_proc;
};

} // namespace ddprof
