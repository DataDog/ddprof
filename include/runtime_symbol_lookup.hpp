// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"
#include "ddres_def.hpp"
#include "map_utils.hpp"
#include "symbol_map.hpp"
#include "symbol_table.hpp"
#include "unique_fd.hpp"

#include <array>
#include <unordered_map>

struct ddog_prof_ProfilesDictionary;

namespace ddprof {

class RuntimeSymbolLookup {
public:
  struct Stats {
    uint32_t _nb_jit_reads = {};
    uint32_t _nb_failed_lookups = {};
    uint32_t _symbol_count = {};
  };

  explicit RuntimeSymbolLookup(std::string_view path_to_proc)
      : _path_to_proc(path_to_proc), _stats{} {}

  SymbolIdx_t get_or_insert_jitdump(pid_t pid, ProcessAddress_t pc,
                                    SymbolTable &symbol_table,
                                    const ddog_prof_ProfilesDictionary *dict,
                                    std::string_view jitdump_path);

  SymbolIdx_t get_or_insert(pid_t pid, ProcessAddress_t pc,
                            SymbolTable &symbol_table,
                            const ddog_prof_ProfilesDictionary *dict);

  void erase(pid_t pid) { _pid_map.erase(pid); }

  void cycle() {
    ++_cycle_counter;
    _stats = {};
  }

  Stats get_stats() const {
    Stats ret = _stats;
    ret._symbol_count = 0;
    for (const auto &map : _pid_map) {
      ret._symbol_count += map.second._map.size();
    }
    return ret;
  }

private:
  using FailedCycle = HeterogeneousLookupStringMap<uint32_t>;

  // Per-(pid, jitdump path) state used to decide when to retry a jitdump
  // read after a soft miss (file readable, but the sampled PC was not in
  // the records we already ingested). See get_or_insert_jitdump.
  struct JitdumpRetryState {
    int64_t size_bytes = 0;
    inode_t inode = 0;
    uint32_t misses_since_check = 0;
  };
  using JitdumpRetryMap = HeterogeneousLookupStringMap<JitdumpRetryState>;

  struct SymbolInfo {
    SymbolMap _map;
    FailedCycle _failed_cycle;
    JitdumpRetryMap _jitdump_retry;
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
                          SymbolMap &symbol_map, SymbolTable &symbol_table,
                          const ddog_prof_ProfilesDictionary *dict);

  // Best-effort stat of the jitdump file using the same candidate paths
  // as fill_from_jitdump. Returns true on success and fills *size and
  // *inode. Returns false if no candidate path was stat-able.
  bool stat_jitdump(pid_t pid, std::string_view jitdump_path, int64_t *size,
                    inode_t *inode) const;

  // Counter-gated stat to detect that the jitdump file has changed
  // (grown or been replaced) since the last read. Returns true when a
  // change is observed, in which case the retry state is updated to the
  // freshly-observed size/inode so the change is not reported twice.
  bool check_jitdump_changed(SymbolInfo &symbol_info, pid_t pid,
                             std::string_view jitdump_path);

  // Parse the jitdump file and refresh the retry-state book-keeping so
  // subsequent retries only fire when the file changes again.
  DDRes read_jitdump(SymbolInfo &symbol_info, pid_t pid,
                     std::string_view jitdump_path, SymbolTable &symbol_table,
                     const ddog_prof_ProfilesDictionary *dict);

  DDRes fill_from_perfmap(int pid, SymbolMap &symbol_map,
                          SymbolTable &symbol_table,
                          const ddog_prof_ProfilesDictionary *dict);

  UniqueFile perfmaps_open(int pid, const char *path_to_perfmap);

  bool has_lookup_failure(const SymbolInfo &symbol_info,
                          std::string_view path) const {
    const auto it = symbol_info._failed_cycle.find(path);
    if (it != symbol_info._failed_cycle.end()) {
      // failure during this cycle
      return it->second == _cycle_counter;
    }
    return false;
  }

  // Soft-miss retry budget: a soft miss flags the (pid, path) pair as
  // "already tried this cycle" so we don't reparse the jitdump for every
  // subsequent missed PC. Without any retry mechanism however, a single
  // unlucky first sample (e.g. fired while Julia was still flushing
  // jit-<pid>.dump) leaves the whole cycle with 0 symbolized JIT frames.
  //
  // To recover, every k_jitdump_retry_check_every soft misses we stat the
  // jitdump file. If it has grown / been replaced since the last read we
  // unfreeze and reparse; otherwise we keep the freeze cheaply.
  //
  // Cost note: with 99 Hz CPU sampling on up to ~20 cores, the worst case
  // (every sample misses) is ~99 * 20 / 64 ~= 30 stat() calls per second
  // for the whole profiler. In practice the JIT map hits dominate and
  // stat is invoked far less.
  static constexpr uint32_t k_jitdump_retry_check_every = 64;

  // Exposed for unit tests so tests can validate the retry budget without
  // having to perform thousands of lookups.
public:
  static constexpr uint32_t jitdump_retry_check_every() {
    return k_jitdump_retry_check_every;
  }

private:
  void flag_lookup_failure(SymbolInfo &symbol_info, std::string_view path) {
    const auto it = symbol_info._failed_cycle.find(path);
    // Written this way, we save up on creating strings
    // only the slow path will create a string for the path
    if (it != symbol_info._failed_cycle.end()) {
      it->second = _cycle_counter;
    } else {
      symbol_info._failed_cycle[std::string(path)] = _cycle_counter;
    }
    ++_stats._nb_failed_lookups;
  }

  static bool should_skip_symbol(std::string_view symbol);

  static bool insert_or_replace(std::string_view symbol,
                                ProcessAddress_t address, Offset_t code_size,
                                SymbolMap &symbol_map,
                                SymbolTable &symbol_table,
                                const ddog_prof_ProfilesDictionary *dict);

  static constexpr std::array<const std::string_view, 1>
      _ignored_symbols_start = {{
          // dotnet symbols we skip all start by stub<
          "stub<",
      }};

  PidUnorderedMap _pid_map;
  std::string _path_to_proc;
  Stats _stats;
  uint32_t _cycle_counter{1};
};

} // namespace ddprof
