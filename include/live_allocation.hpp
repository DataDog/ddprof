// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"
#include "unlikely.hpp"
#include "unwind_output_hash.hpp"

#include <cstddef>
#include <set>
#include <sys/types.h>
#include <unordered_map>
#include <vector>

namespace ddprof {

template <typename T>
T &access_resize(std::vector<T> &v, size_t index,
                 const T &default_value = T()) {
  if (unlikely(index >= v.size())) {
    v.resize(index + 1, default_value);
  }
  return v[index];
}

class LiveAllocation {
public:
  struct SmapsEntry {
    ProcessAddress_t start;
    ProcessAddress_t end;
    size_t rss_kb;
    size_t accounted_size{};
  };

  struct ValueAndCount {
    int64_t _value = 0;
    int64_t _count = 0;
  };

  struct StackAndMapping {
    const UnwindOutput *uw_output_ptr; // Pointer to an UnwindOutput in a set
    ProcessAddress_t start_mmap;       // Start of associated mapping

    bool operator==(const StackAndMapping &other) const {
      return uw_output_ptr == other.uw_output_ptr &&
          start_mmap == other.start_mmap;
    }
  };

  struct StackAndMappingHash {
    std::size_t operator()(const StackAndMapping &s) const {
      size_t seed = std::hash<const UnwindOutput *>{}(s.uw_output_ptr);
      hash_combine(seed, std::hash<ProcessAddress_t>{}(s.start_mmap));
      return seed;
    }
  };

  using PprofStacks =
      std::unordered_map<StackAndMapping, ValueAndCount, StackAndMappingHash>;
  using MappingValuesMap = std::unordered_map<ProcessAddress_t, ValueAndCount>;

  struct ValuePerAddress {
    int64_t _value = 0;
    PprofStacks::value_type *_unique_stack = nullptr;
  };

  using AddressMap = std::unordered_map<uintptr_t, ValuePerAddress>;

  struct PidStacks {
    AddressMap _address_map;
    PprofStacks _unique_stacks;
    std::set<UnwindOutput>
        unwind_output_set; // Set to store all unique UnwindOutput objects
    std::vector<SmapsEntry> entries;
    MappingValuesMap
        mapping_values; // New map to track memory usage per mapping
  };

  using PidMap = std::unordered_map<pid_t, PidStacks>;
  using WatcherVector = std::vector<PidMap>;

  void register_allocation(const UnwindOutput &uo, uintptr_t addr, size_t size,
                           int watcher_pos, pid_t pid) {
    PidMap &pid_map = access_resize(_watcher_vector, watcher_pos);
    PidStacks &pid_stacks = pid_map[pid];
    if (pid_stacks.entries.empty()) {
      pid_stacks.entries = parse_smaps(pid);
    }
    register_allocation_internal(uo, addr, size, pid_stacks);
  }

  void register_deallocation(uintptr_t addr, int watcher_pos, pid_t pid) {
    PidMap &pid_map = access_resize(_watcher_vector, watcher_pos);
    PidStacks &pid_stacks = pid_map[pid];
    if (!register_deallocation_internal(addr, pid_stacks)) {
      ++_stats._unmatched_deallocations;
    }
  }

  void clear_pid_for_watcher(int watcher_pos, pid_t pid) {
    PidMap &pid_map = access_resize(_watcher_vector, watcher_pos);
    pid_map.erase(pid);
  }

  void clear_pid(pid_t pid) {
    for (auto &pid_map : _watcher_vector) {
      pid_map.erase(pid);
    }
  }

  [[nodiscard]] unsigned get_nb_unmatched_deallocations() const {
    return _stats._unmatched_deallocations;
  }

  static std::vector<SmapsEntry> parse_smaps(pid_t pid);

  static int64_t upscale_with_mapping(const PprofStacks::value_type &stack,
                                      PidStacks &pid_stacks);

  void cycle() { _stats = {}; }

  // no lint to avoid warning about member being public (should be refactored)
  WatcherVector _watcher_vector; // NOLINT
private:
  static bool register_deallocation_internal(uintptr_t address,
                                             PidStacks &pid_stacks);

  static bool register_allocation_internal(const UnwindOutput &uo,
                                           uintptr_t address, int64_t value,
                                           PidStacks &pid_stacks);

  struct {
    unsigned _unmatched_deallocations = {};
  } _stats;
};

} // namespace ddprof
