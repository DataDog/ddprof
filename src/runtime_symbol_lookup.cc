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

const std::unordered_set<std::string> RuntimeSymbolLookup::_ignored_symbols = {
    // dotnet symbols
    std::string("GenerateResolveStub"),
    std::string("GenerateDispatchStub"),
    std::string("GenerateLookupStub"),
    std::string("AllocateTemporaryEntryPoints"),
};

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
  if (IsDDResNotOK(jitdump_read(std::string_view(buf, n), jitdump))) {
    if (IsDDResNotOK(jitdump_read(jitdump_path, jitdump))) {
      // adding an empty element to flag the fact there was an attempt
      return ddres_error(DD_WHAT_JIT);
    }
  }

  for (const JITRecordCodeLoad &code_load : jitdump.code_load) {
    // elements are ordered
    SymbolMap::FindRes find_res = symbol_map.find_closest(code_load.code_addr);
    // we assume that we already came across this symbol
    if (!find_res.second) {
      symbol_map.emplace_hint(
          find_res.first, code_load.code_addr,
          SymbolSpan(code_load.code_addr + code_load.code_size,
                     symbol_table.size()));
      // we don't need demangling in most languages.
      // we can consider removing this if it becomes a hot path
      std::string demangle_func = llvm::demangle(code_load.func_name);
      symbol_table.emplace_back(
          Symbol(code_load.func_name, demangle_func, 0, "jit"));
    }
  }
  // todo we can add file and inlined functions with debug info
  return ddres_init();
}

bool RuntimeSymbolLookup::should_skip_symbol(const char *symbol) {
  return _ignored_symbols.find(symbol) != _ignored_symbols.end();
}

DDRes RuntimeSymbolLookup::fill_from_perfmap(int pid, SymbolMap &symbol_map,
                                             SymbolTable &symbol_table) {
  FILE *pmf = perfmaps_open(pid, "/tmp");
  if (pmf == nullptr) {
    LG_DBG("Unable to read perfmap file (PID%d)", pid);
    return ddres_error(DD_WHAT_JIT);
  }
  defer { fclose(pmf); };

  LG_DBG("Loading runtime symbols from (PID%d)", pid);
  char *line = NULL;
  size_t sz_buf = 0;
  char buffer[2048];
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
      LG_DBG("Overflow detected when parsing perfmap (%d)", pid);
      continue;
    }
    SymbolMap::FindRes find_res = symbol_map.find_closest(address);
    if (!find_res.second) {
      // elements are ordered, hints help
      symbol_map.emplace_hint(find_res.first, address,
                              SymbolSpan(end, symbol_table.size()));
      symbol_table.emplace_back(
          Symbol(std::string(buffer), std::string(buffer), 0, "jit"));
    } else {
      LG_DBG("buffer=%s", buffer);
    }
  }
  free(line);
  return ddres_init();
}

SymbolIdx_t
RuntimeSymbolLookup::get_or_insert_jitdump(pid_t pid, ProcessAddress_t pc,
                                           SymbolTable &symbol_table,
                                           std::string_view jitdump_path) {
  SymbolInfo &symbol_info = _pid_map[pid];
  SymbolMap::FindRes find_res = symbol_info._map.find_closest(pc);
  if (!find_res.second && !has_lookup_failure(symbol_info, jitdump_path)) {
    // refresh as we expect there to be new symbols
    if (IsDDResNotOK(fill_from_jitdump(jitdump_path, pid, symbol_info._map,
                                       symbol_table))) {
      flag_lookup_failure(symbol_info, jitdump_path);
      return -1;
    }
  }
  find_res = symbol_info._map.find_closest(pc);
  // Avoid bouncing when we are failing lookups.
  // !This could have a negative impact on symbolisation. To be studied
  if (!find_res.second) {
    flag_lookup_failure(symbol_info, jitdump_path);
  }
  return find_res.second ? find_res.first->second.get_symbol_idx() : -1;
}

SymbolIdx_t RuntimeSymbolLookup::get_or_insert(pid_t pid, ProcessAddress_t pc,
                                               SymbolTable &symbol_table) {
  SymbolInfo &symbol_info = _pid_map[pid];
  SymbolMap::FindRes find_res = symbol_info._map.find_closest(pc);

  // Only check the file if we did not get failures in this cycle (for this pid)
  if (!find_res.second &&
      !has_lookup_failure(symbol_info, std::string("perfmap"))) {
    fill_from_perfmap(pid, symbol_info._map, symbol_table);
  }
  find_res = symbol_info._map.find_closest(pc);
  if (!find_res.second) {
    flag_lookup_failure(symbol_info, "perfmap");
  }
  return find_res.second ? find_res.first->second.get_symbol_idx() : -1;
}

} // namespace ddprof
