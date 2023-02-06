// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <iostream>
#include <limits>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "ddres.hpp"
#include "defer.hpp"
#include "logger.hpp"
#include "runtime_symbol_lookup.hpp"
#include "unlikely.hpp"

#include "llvm/Demangle/Demangle.h"

#include "jit/jitdump.hpp"

namespace ddprof {

FILE *RuntimeSymbolLookup::perfmaps_open(int pid,
                                         const char *path_to_perfmap = "") {
  char buf[1024] = {0};
  auto n = snprintf(buf, 1024, "%s/proc/%d/root%s/perf-%d.map",
                    _path_to_proc.c_str(), pid, path_to_perfmap, pid);
  if (n >= 1024) { // unable to snprintf everything
    return nullptr;
  }
  FILE *perfmap_file = fopen(buf, "r");
  if (perfmap_file) {
    return perfmap_file;
  }
  // attempt in local namespace
  snprintf(buf, 1024, "%s/perf-%d.map", path_to_perfmap, pid);
  return fopen(buf, "r");
}

DDRes RuntimeSymbolLookup::fill_from_jitdump(std::string_view jitdump_path,
                                             pid_t pid, SymbolMap &symbol_map,
                                             SymbolTable &symbol_table) {

  char buf[1024] = {0};
  auto n = snprintf(buf, 1024, "%s/proc/%d/root%s", _path_to_proc.c_str(), pid,
                    jitdump_path.data());
  if (n >= 1024) { // unable to snprintf everything
    DDRES_RETURN_ERROR_LOG(DD_WHAT_JIT, "Unable to create path to jitdump");
  }

  JITDump jitdump;
  if (IsDDResNotOK(jit_read(std::string_view(buf, n), jitdump))) {
    if (IsDDResNotOK(jit_read(jitdump_path, jitdump))) {
      // adding an empty element to avoid re-parsing this file
      symbol_map.emplace(0, SymbolSpan());
      return ddres_error(DD_WHAT_JIT);
    }
  }
  auto it = symbol_map.end();

  for (const JITRecordCodeLoad &code_load : jitdump.code_load) {
    // elements are ordered
    it = symbol_map.emplace_hint(
        it, code_load.code_addr,
        SymbolSpan(code_load.code_addr + code_load.code_size,
                   symbol_table.size()));

    std::string demangle_func = llvm::demangle(code_load.func_name);
    symbol_table.emplace_back(
        Symbol(code_load.func_name, demangle_func, 0, "jit"));
    LG_DBG("insert %s - %s", code_load.func_name.c_str(),
           demangle_func.c_str());
  }
  // todo we can add file and inlined functions with debug info
  return ddres_init();
}

bool should_skip_symbol(const char *symbol) {
  // dotnet symbols
  return strstr(symbol, "GenerateResolveStub") != nullptr ||
      strstr(symbol, "GenerateDispatchStub") != nullptr ||
      strstr(symbol, "GenerateLookupStub") != nullptr ||
      strstr(symbol, "AllocateTemporaryEntryPoints") != nullptr;
}

void RuntimeSymbolLookup::fill_from_perfmap(int pid, SymbolMap &symbol_map,
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
    char address_buff[33]; // max size of 16 (as it should be hexa for uint64)
    char size_buff[33];
    // Avoid considering any symbols beyond 300 chars
    if (3 !=
            sscanf(line, "%16s %8s %300[^\t\n]", address_buff, size_buff,
                   buffer) ||
        should_skip_symbol(buffer)) {
      continue;
    }
    ProcessAddress_t address = std::strtoul(address_buff, nullptr, 16);
    Offset_t code_size = std::strtoul(size_buff, nullptr, 16);
    // check for conversion issues
    if (!address || !code_size ||
        address == std::numeric_limits<ProcessAddress_t>::max() ||
        code_size == std::numeric_limits<ProcessAddress_t>::max()) {
      continue;
    }
    ProcessAddress_t end = address + code_size - 1;
#ifdef DEBUG
    LG_NFO("Attempt insert at %lx --> %lx / %s", address, end, buffer);
#endif
    if (unlikely(address >
                 std::numeric_limits<ProcessAddress_t>::max() - code_size)) {
      // Ignore overflow
      continue;
    }

    // elements are ordered
    it = symbol_map.emplace_hint(it, address,
                                 SymbolSpan(end, symbol_table.size()));
    symbol_table.emplace_back(
        Symbol(std::string(buffer), std::string(buffer), 0, "jit"));
  }
  free(line);
}

SymbolIdx_t
RuntimeSymbolLookup::get_or_insert_jitdump(pid_t pid, ProcessAddress_t pc,
                                           SymbolTable &symbol_table,
                                           std::string_view jitdump_path) {
  SymbolMap &symbol_map = _pid_map[pid];
  if (symbol_map.empty()) {
    if (IsDDResNotOK(
            fill_from_jitdump(jitdump_path, pid, symbol_map, symbol_table))) {
      // It could be there was a write at that moment
      LG_DBG("Error parsing jit dump %s", jitdump_path.data());
      return -1;
    }
  }
  SymbolMap::FindRes find_res = symbol_map.find_closest(pc);
  return find_res.second ? find_res.first->second.get_symbol_idx() : -1;
}

SymbolIdx_t RuntimeSymbolLookup::get_or_insert(pid_t pid, ProcessAddress_t pc,
                                               SymbolTable &symbol_table) {
  SymbolMap &symbol_map = _pid_map[pid];
  // TODO : how do we know we need to refresh the symbol map ?
  // A solution can be to poll + inotify ? Though where would this poll be
  // handled ?

  if (symbol_map.empty()) {
    fill_from_perfmap(pid, symbol_map, symbol_table);
  }

  SymbolMap::FindRes find_res = symbol_map.find_closest(pc);
  if (find_res.second) {
    return find_res.first->second.get_symbol_idx();
  }
  return -1;
}

} // namespace ddprof
