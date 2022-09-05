// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <iostream>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "defer.hpp"
#include "logger.hpp"
#include "runtime_symbol_lookup.hpp"

namespace ddprof {

FILE *RuntimeSymbolLookup::perfmaps_open(int pid,
                                         const char *path_to_perfmap = "") {
  char buf[1024] = {0};
  auto n = snprintf(buf, 1024, "%s/proc/%d/root%s/perf-%d.map",
                    _path_to_proc.c_str(), pid, path_to_perfmap, pid);
  if (n >= 1024) { // unable to snprintf everything
    return nullptr;
  }
  LG_NFO(" -- buff = %s", buf);
  FILE *perfmap_file = fopen(buf, "r");
  if (perfmap_file) {
    return perfmap_file;
  }
  // attempt in local namespace
  snprintf(buf, 1024, "%s/perf-%d.map", path_to_perfmap, pid);
  return fopen(buf, "r");
}

bool should_skip_symbol(const char *symbol) {
  return strstr(symbol, "GenerateResolveStub") != nullptr ||
      strstr(symbol, "GenerateDispatchStub") != nullptr ||
      strstr(symbol, "GenerateLookupStub") != nullptr ||
      strstr(symbol, "AllocateTemporaryEntryPoints") != nullptr;
}

void RuntimeSymbolLookup::fill_perfmap_from_file(int pid, SymbolMap &symbol_map,
                                                 SymbolTable &symbol_table) {
  FILE *pmf = perfmaps_open(pid, "/tmp");
  symbol_map.clear();
  if (pmf == nullptr) {
    // Add a single fake symbol to avoid bouncing
    symbol_map.emplace(0, SymbolSpan());
    LG_DBG("No runtime symbols (PID%d)", pid);
    return;
  }
  defer { fclose(pmf); };

  LG_DBG("Loading runtime symbols from (PID%d)", pid);
  char *line = NULL;
  size_t sz_buf = 0;
  char buffer[2048];
  auto it = symbol_map.end();
  while (-1 != getline(&line, &sz_buf, pmf)) {
    uint64_t address;
    uint32_t code_size;
    if (3 != sscanf(line, "%lx %x %[^\t\n]", &address, &code_size, buffer) ||
        should_skip_symbol(buffer)) {
      continue;
    }
    // elements are ordered
    it = symbol_map.emplace_hint(
        it, address, SymbolSpan(address + code_size - 1, symbol_table.size()));
    symbol_table.emplace_back(
        Symbol(std::string(buffer), std::string(buffer), 0, "unknown"));
  }
  free(line);
}

SymbolIdx_t RuntimeSymbolLookup::get_or_insert(pid_t pid, ProcessAddress_t pc,
                                               SymbolTable &symbol_table) {
  SymbolMap &symbol_map = _pid_map[pid];
  // TODO : how do we know we need to refresh the symbol map ?
  // A solution can be to poll + inotify ? Though where would this poll be
  // handled ?

  if (symbol_map.empty()) {
    fill_perfmap_from_file(pid, symbol_map, symbol_table);
  }

  SymbolMap::FindRes find_res = symbol_map.find_closest(pc);
  if (find_res.second) {
    return find_res.first->second.get_symbol_idx();
  }
  return -1;
}

} // namespace ddprof
