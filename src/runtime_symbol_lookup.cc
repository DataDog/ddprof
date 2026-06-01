// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "runtime_symbol_lookup.hpp"

#include "ddog_profiling_utils.hpp"
#include "ddres.hpp"
#include "defer.hpp"
#include "jit/jitdump.hpp"
#include "logger.hpp"
#include "procutils.hpp"
#include "unlikely.hpp"

#include <absl/strings/substitute.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace ddprof {

// 00007F78F5230000 d8 stub<1> AllocateTemporaryEntryPoints<PRECODE_STUB>
// 00007F78F52300D8 78 stub<2> AllocateTemporaryEntryPoints<PRECODE_STUB>
// 00007F78F5230150 18 stub<3> AllocateTemporaryEntryPoints<PRECODE_STUB>

UniqueFile
RuntimeSymbolLookup::perfmaps_open(int pid, const char *path_to_perfmap = "") {
  char buf[PATH_MAX];
  auto n = snprintf(buf, std::size(buf), "%s/proc/%d/root%s/perf-%d.map",
                    _path_to_proc.c_str(), pid, path_to_perfmap, pid);
  if (static_cast<unsigned>(n) >=
      std::size(buf)) { // unable to snprintf everything
    return nullptr;
  }
  UniqueFile perfmap_file{fopen(buf, "r")};
  if (perfmap_file) {
    return perfmap_file;
  }
  // attempt in local namespace
  snprintf(buf, std::size(buf), "%s/perf-%d.map", path_to_perfmap, pid);
  LG_DBG("Open perf-map %s", buf);
  return UniqueFile{fopen(buf, "r")};
}

bool RuntimeSymbolLookup::insert_or_replace(
    std::string_view symbol, ProcessAddress_t address, Offset_t code_size,
    SymbolMap &symbol_map, SymbolTable &symbol_table,
    const ddog_prof_ProfilesDictionary *dict) {
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

  SymbolMap::FindRes const find_res = symbol_map.find_closest(address);
  if (!find_res.second) {
    symbol_map.emplace_hint(
        find_res.first, address,
        SymbolSpan(address + code_size - 1, symbol_table.size()));
    symbol_table.emplace_back(make_symbol(std::string(symbol), 0, "jit", dict));
  } else {
    // todo managing range erase (we can overlap with other syms)
    SymbolIdx_t const existing = find_res.first->second.get_symbol_idx();
    ddog_prof_StringId2 name_id = intern_string(dict, symbol);
    // ProfilesDictionary canonicalizes strings within a dictionary, so handle
    // comparison avoids materializing strings on this hot path.
    if (symbol_table[existing]._function_id &&
        symbol_table[existing]._function_id->name == name_id) {
      find_res.first->second.set_end(address + code_size - 1);
    } else {
      // remove current element (as start can be different)
      symbol_map.erase(find_res.first);
      symbol_map.emplace(address,
                         SymbolSpan(address + code_size - 1, existing));
      Symbol &existing_symbol = symbol_table[existing];
      existing_symbol._function_id = intern_function(dict, symbol, "jit");
    }
  }

  return true;
}
namespace {
bool is_absolute_path(std::string_view path) { return path.front() == '/'; }
} // namespace

DDRes RuntimeSymbolLookup::fill_from_jitdump(
    std::string_view jitdump_path, pid_t pid, SymbolMap &symbol_map,
    SymbolTable &symbol_table, const ddog_prof_ProfilesDictionary *dict) {
  const std::string path = is_absolute_path(jitdump_path)
      ? absl::Substitute("$0/proc/$1/root$2", _path_to_proc, pid,
                         jitdump_path)
      : // For relative path, use the current working directory
      absl::Substitute("$0/proc/$1/cwd/$2", _path_to_proc, pid, jitdump_path);

  JITDump jitdump;
  DDRes res = jitdump_read(path, jitdump);
  if (IsDDResNotOK(res) && res._what == DD_WHAT_NO_JIT_FILE) {
    // retry with different path
    res = jitdump_read(jitdump_path, jitdump);
    if (IsDDResFatal(res)) {
      if (res._what == DD_WHAT_NO_JIT_FILE) {
        LG_WRN("Unable to read jitdump file at %.*s",
               static_cast<int>(jitdump_path.size()), jitdump_path.data());
      }
      // Stop if fatal error
      return res;
    }
  }

  for (const JITRecordCodeLoad &code_load : jitdump.code_load) {
    insert_or_replace(code_load.func_name, code_load.code_addr,
                      code_load.code_size, symbol_map, symbol_table, dict);
  }
  // todo we can add file and inlined functions with debug info
  return {};
}

bool RuntimeSymbolLookup::should_skip_symbol(std::string_view symbol) {
  // we could consider making this more efficient if the table grows
  return std::ranges::any_of(_ignored_symbols_start, [&symbol](auto &el) {
    return symbol.starts_with(el);
  });
}

DDRes RuntimeSymbolLookup::fill_from_perfmap(
    int pid, SymbolMap &symbol_map, SymbolTable &symbol_table,
    const ddog_prof_ProfilesDictionary *dict) {
  auto pmf{perfmaps_open(pid, "/tmp")};
  if (!pmf) {
    LG_DBG("Unable to read perfmap file (PID%d)", pid);
    return ddres_error(DD_WHAT_NO_JIT_FILE);
  }

  LG_DBG("Loading runtime symbols from (PID%d)", pid);
  char *line = nullptr;
  size_t sz_buf = 0;
  char buffer[PATH_MAX];
  while (-1 != getline(&line, &sz_buf, pmf.get())) {
    constexpr size_t k_uint64_hex_rep_size = (2 * sizeof(uint64_t)) + 1;
    char address_buff[k_uint64_hex_rep_size]; // max size of 16 (as it should be
                                              // hexa for uint64)
    char size_buff[k_uint64_hex_rep_size];
    // Avoid considering any symbols beyond 300 chars
    if (3 !=
        sscanf(line, "%16s %8s %300[^\t\n]", address_buff, size_buff, buffer)) {
      continue;
    }
    constexpr int hexadecimal_base = 16;
    ProcessAddress_t const address =
        std::strtoul(address_buff, nullptr, hexadecimal_base);
    Offset_t const code_size =
        std::strtoul(size_buff, nullptr, hexadecimal_base);
    insert_or_replace(buffer, address, code_size, symbol_map, symbol_table,
                      dict);
  }
  free(line);
  return {};
}

bool RuntimeSymbolLookup::stat_jitdump(pid_t pid, std::string_view jitdump_path,
                                       int64_t *size, inode_t *inode) const {
  // The path advertised by perf for the jitdump mapping is observed from
  // inside the target's mount namespace. When ddprof runs in a different
  // namespace (typically the host, with the target in a container) that
  // raw path is not directly reachable. Resolve it the same way
  // fill_from_jitdump does so growth checks see the same file the parser
  // would actually read:
  //   - absolute path -> reach via /proc/<pid>/root/<path>
  //   - relative path -> reach via /proc/<pid>/cwd/<path>
  //   - if neither works (e.g. ddprof runs in the same namespace as the
  //     target) fall back to the raw path.
  const std::string proc_path = is_absolute_path(jitdump_path)
      ? absl::Substitute("$0/proc/$1/root$2", _path_to_proc, pid, jitdump_path)
      : absl::Substitute("$0/proc/$1/cwd/$2", _path_to_proc, pid, jitdump_path);

  if (get_file_inode(proc_path.c_str(), inode, size)) {
    return true;
  }
  const std::string fallback(jitdump_path);
  return get_file_inode(fallback.c_str(), inode, size);
}

bool RuntimeSymbolLookup::check_jitdump_changed(SymbolInfo &symbol_info,
                                                pid_t pid,
                                                std::string_view jitdump_path) {
  auto [retry_it, inserted] =
      symbol_info._jitdump_retry.try_emplace(std::string(jitdump_path));
  JitdumpRetryState &retry = retry_it->second;
  if (++retry.misses_since_check < k_jitdump_retry_check_every) {
    return false;
  }
  retry.misses_since_check = 0;
  int64_t size = 0;
  inode_t inode = 0;
  if (!stat_jitdump(pid, jitdump_path, &size, &inode)) {
    // Path is currently unreachable (e.g. //anon). Keep the freeze
    // without churning; next cycle naturally retries.
    return false;
  }
  if (size == retry.size_bytes && inode == retry.inode) {
    return false;
  }
  retry.size_bytes = size;
  retry.inode = inode;
  return true;
}

DDRes RuntimeSymbolLookup::read_jitdump(
    SymbolInfo &symbol_info, pid_t pid, std::string_view jitdump_path,
    SymbolTable &symbol_table, const ddog_prof_ProfilesDictionary *dict) {
  ++_stats._nb_jit_reads;
  DDRes res = fill_from_jitdump(jitdump_path, pid, symbol_info._map,
                                symbol_table, dict);
  if (IsDDResFatal(res)) {
    return res;
  }
  // Refresh the cached size/inode so subsequent retries only fire on growth.
  auto [retry_it, inserted] =
      symbol_info._jitdump_retry.try_emplace(std::string(jitdump_path));
  stat_jitdump(pid, jitdump_path, &retry_it->second.size_bytes,
               &retry_it->second.inode);
  retry_it->second.misses_since_check = 0;
  return res;
}

SymbolIdx_t RuntimeSymbolLookup::get_or_insert_jitdump(
    pid_t pid, ProcessAddress_t pc, SymbolTable &symbol_table,
    const ddog_prof_ProfilesDictionary *dict, std::string_view jitdump_path) {
  SymbolInfo &symbol_info = _pid_map[pid];
  SymbolMap::FindRes find_res = symbol_info._map.find_closest(pc);
  if (find_res.second) {
    return find_res.first->second.get_symbol_idx();
  }

  // Two reasons to (re)read the file:
  //   1. We have never read it (or we did, but a new cycle has cleared the
  //      previous failure flag).
  //   2. We've already given up this cycle, but a periodic stat shows the
  //      file has grown or been replaced since the last read.
  const bool first_attempt = !has_lookup_failure(symbol_info, jitdump_path);
  const bool should_read =
      first_attempt || check_jitdump_changed(symbol_info, pid, jitdump_path);

  if (should_read) {
    if (IsDDResFatal(
            read_jitdump(symbol_info, pid, jitdump_path, symbol_table, dict))) {
      // Some warnings can be expected with incomplete files.
      flag_lookup_failure(symbol_info, jitdump_path);
      return -1;
    }
    find_res = symbol_info._map.find_closest(pc);
  }

  // Soft miss (or hard miss with no read attempted): freeze for the cycle.
  // !This could have a negative impact on symbolisation. To be studied.
  if (!find_res.second) {
    flag_lookup_failure(symbol_info, jitdump_path);
    return -1;
  }
  return find_res.first->second.get_symbol_idx();
}

SymbolIdx_t
RuntimeSymbolLookup::get_or_insert(pid_t pid, ProcessAddress_t pc,
                                   SymbolTable &symbol_table,
                                   const ddog_prof_ProfilesDictionary *dict) {
  SymbolInfo &symbol_info = _pid_map[pid];
  SymbolMap::FindRes find_res = symbol_info._map.find_closest(pc);

  // Only check the file if we did not get failures in this cycle (for this pid)
  if (!find_res.second && !has_lookup_failure(symbol_info, "perfmap")) {
    ++_stats._nb_jit_reads;
    fill_from_perfmap(pid, symbol_info._map, symbol_table, dict);
    find_res = symbol_info._map.find_closest(pc);
  }
  if (!find_res.second) {
    flag_lookup_failure(symbol_info, "perfmap");
  }
  return find_res.second ? find_res.first->second.get_symbol_idx() : -1;
}

} // namespace ddprof
