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

#include "jit/jitdump.hpp"

namespace ddprof {

// 00007F78F5230000 d8 stub<1> AllocateTemporaryEntryPoints<PRECODE_STUB>
// 00007F78F52300D8 78 stub<2> AllocateTemporaryEntryPoints<PRECODE_STUB>
// 00007F78F5230150 18 stub<3> AllocateTemporaryEntryPoints<PRECODE_STUB>

FILE *RuntimeSymbolLookup::perfmaps_open(int pid,
                                         const char *path_to_perfmap = "") {
  char buf[1024];
  auto n = snprintf(buf, std::size(buf), "%s/proc/%d/root%s/perf-%d.map",
                    _path_to_proc.c_str(), pid, path_to_perfmap, pid);
  if (n >= 1024) { // unable to snprintf everything
    return nullptr;
  }
  FILE *perfmap_file = fopen(buf, "r");
  if (perfmap_file) {
    return perfmap_file;
  }
  // attempt in local namespace
  snprintf(buf, std::size(buf), "%s/perf-%d.map", path_to_perfmap, pid);
  LG_DBG("Open perf-map %s", buf);
  return fopen(buf, "r");
}

bool RuntimeSymbolLookup::insert_or_replace(const char *symbol,
                                            ProcessAddress_t address,
                                            Offset_t code_size,
                                            SymbolMap &symbol_map,
                                            SymbolTable &symbol_table) {
  if (should_skip_symbol(symbol)) {
    return false;
  }

  if (!address || !code_size ||
      address == std::numeric_limits<ProcessAddress_t>::max() ||
      code_size == std::numeric_limits<ProcessAddress_t>::max()) {
    return false;
  }

  if (unlikely(address >
               std::numeric_limits<ProcessAddress_t>::max() - code_size)) {
    return false;
  }

  SymbolMap::FindRes find_res = symbol_map.find_closest(address);
  if (!find_res.second) {
    symbol_map.emplace_hint(
        find_res.first, address,
        SymbolSpan(address + code_size - 1, symbol_table.size()));
    symbol_table.emplace_back(Symbol(symbol, symbol, 0, "jit"));
  } else {
    // todo managing range erase (we can overal with other syms)
    SymbolIdx_t existing = find_res.first->second.get_symbol_idx();
#ifdef DEBUG
    LG_DBG("Existyng sym -- %s (%lx-%lx)",
           symbol_table[existing]._demangle_name.c_str(), find_res.first->first,
           find_res.first->second.get_end());
    LG_DBG("New sym -- %s (%lx-%lx)", code_load.func_name.c_str(),
           code_load.code_addr, code_load.code_size + code_load.code_addr);
#endif
    if (symbol_table[existing]._demangle_name == symbol) {
      // nothing to do (unlikely size would change ?)
    } else {
      // remove current element (as start can be different)
      symbol_map.erase(find_res.first);
      symbol_map.emplace(
          address, SymbolSpan(address + code_size - 1, symbol_table.size()));
      symbol_table[existing]._demangle_name = symbol;
      symbol_table[existing]._symname = symbol;
    }
  }

  return true;
}

DDRes RuntimeSymbolLookup::fill_from_jitdump(std::string_view jitdump_path,
                                             pid_t pid, SymbolMap &symbol_map,
                                             SymbolTable &symbol_table) {
  char buf[1024];
  auto n = snprintf(buf, std::size(buf), "%s/proc/%d/root%s",
                    _path_to_proc.c_str(), pid, jitdump_path.data());
  if (n >= std::size(buf)) { // unable to snprintf everything
    DDRES_RETURN_ERROR_LOG(DD_WHAT_JIT, "Unable to create path to jitdump");
  }

  JITDump jitdump;
  DDRes res = jitdump_read(std::string_view(buf, n), jitdump);
  if (IsDDResNotOK(res) && res._what == DD_WHAT_NO_JIT_FILE) {
    // retry different path
    res = jitdump_read(jitdump_path, jitdump);
    if (IsDDResFatal(res)) {
      // Stop if fatal error
      return res;
    }
  }

  for (const JITRecordCodeLoad &code_load : jitdump.code_load) {
    insert_or_replace(code_load.func_name, code_load.code_addr,
                      code_load.code_size, symbol_map, symbol_table);
  }
  // todo we can add file and inlined functions with debug info
  return ddres_init();
}

bool RuntimeSymbolLookup::should_skip_symbol(std::string_view symbol) const {
  // we could consider making this more efficient if the table grows
  for (const auto &el : _ignored_symbols_start) {
    if (symbol.starts_with(el)) {
      return true;
    }
  }
  return false;
}

DDRes RuntimeSymbolLookup::fill_from_perfmap(int pid, SymbolMap &symbol_map,
                                             SymbolTable &symbol_table) {
  FILE *pmf = perfmaps_open(pid, "/tmp");
  if (pmf == nullptr) {
    LG_DBG("Unable to read perfmap file (PID%d)", pid);
    return ddres_error(DD_WHAT_NO_JIT_FILE);
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
        sscanf(line, "%16s %8s %300[^\t\n]", address_buff, size_buff, buffer)) {
      continue;
    }
    std::string sym = std::string(buffer);
    ProcessAddress_t address = std::strtoul(address_buff, nullptr, 16);
    Offset_t code_size = std::strtoul(size_buff, nullptr, 16);
    insert_or_replace(sym, address, code_size, symbol_map, symbol_table);
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
    ++_stats._nb_jit_reads;
    if (IsDDResFatal(fill_from_jitdump(jitdump_path, pid, symbol_info._map,
                                       symbol_table))) {
      // Some warnings can be expected with incomplete files
      flag_lookup_failure(symbol_info, jitdump_path);
      return -1;
    }
    find_res = symbol_info._map.find_closest(pc);
  }
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
    ++_stats._nb_jit_reads;
    fill_from_perfmap(pid, symbol_info._map, symbol_table);
    find_res = symbol_info._map.find_closest(pc);
  }
  if (!find_res.second) {
    flag_lookup_failure(symbol_info, "perfmap");
  }
  return find_res.second ? find_res.first->second.get_symbol_idx() : -1;
}

} // namespace ddprof
