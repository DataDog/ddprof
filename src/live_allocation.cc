// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "live_allocation.hpp"

#include "logger.hpp"

#include <absl/strings/str_format.h>
#include <cassert>
#include <charconv>

namespace ddprof {
using SmapsEntry = LiveAllocation::SmapsEntry;

bool LiveAllocation::register_deallocation_internal(uintptr_t address,
                                                    PidStacks &pid_stacks) {
  auto &stacks = pid_stacks._unique_stacks;
  auto &address_map = pid_stacks._address_map;
  auto &mapping_values = pid_stacks.mapping_values;

  // Find the ValuePerAddress object corresponding to the address
  auto map_iter = address_map.find(address);
  if (map_iter == address_map.end()) {
    // No element found, nothing to do
    // This means we lost previous events, leading to de-sync between
    // the state of the profiler and the state of the library.
    LG_DBG("Unmatched de-allocation at %lx", address);
    return false;
  }

  ValuePerAddress const &v = map_iter->second;

  // Decrement count and value of the corresponding PprofStacks::value_type
  // object
  if (v._unique_stack) {
    // Adjust the mapping values map for the allocation being deallocated
    ProcessAddress_t mapping_start = v._unique_stack->first.start_mmap;
    mapping_values[mapping_start]._value -= v._value;
    if (mapping_values[mapping_start]._count > 0) {
      --mapping_values[mapping_start]._count;
    }

    v._unique_stack->second._value -= v._value;
    if (v._unique_stack->second._count > 0) {
      --(v._unique_stack->second._count);
    }

    if (v._unique_stack->second._count == 0) {
      // If count reaches 0, remove the UnwindOutput from stacks
      stacks.erase(v._unique_stack->first);
    }
  }

  // Remove the element from the address map
  address_map.erase(map_iter);
  return true;
}

bool LiveAllocation::register_allocation_internal(const UnwindOutput &uo,
                                                  uintptr_t address,
                                                  int64_t value,
                                                  PidStacks &pid_stacks) {

  if (uo.locs.empty()) {
    // Avoid sending empty stacks
    LG_DBG("(LIVE_ALLOC) Avoid registering empty stack");
    return false;
  }

  // Find or insert the UnwindOutput in the set
  auto [uo_iter, inserted] = pid_stacks.unwind_output_set.insert(uo);
  const UnwindOutput *uo_ptr = &(*uo_iter);

  // Find the corresponding smaps entry for the given address
  auto entry = std::lower_bound(pid_stacks.entries.begin(),
                                pid_stacks.entries.end(), address,
                                [](const LiveAllocation::SmapsEntry &l,
                                   uintptr_t addr) { return l.start < addr; });
  // If lower_bound points to the end or the start address is greater than the
  // address, adjust the entry.
  if (entry != pid_stacks.entries.begin() &&
      (entry == pid_stacks.entries.end() || address < entry->start)) {
    --entry; // Move to the previous entry if it's not the beginning
  }
  ProcessAddress_t start_addr = 0;
  if (entry == pid_stacks.entries.end() || address < entry->start) {
    // Address not within any known mapping
    LG_DBG("(LIVE_ALLOC) Address not within any known mapping: %lx", address);
    if (entry != pid_stacks.entries.end())
      LG_DBG("(LIVE_ALLOC) matched entry start: %lx, end: %lx", entry->start,
             entry->end);
  } else {
    start_addr = entry->start;
  }

  // Create or find the PprofStacks::value_type object corresponding to the
  // UnwindOutput
  auto &stacks = pid_stacks._unique_stacks;
  StackAndMapping stack_key{uo_ptr, start_addr};
  auto iter = stacks.find(stack_key);
  if (iter == stacks.end()) {
    iter = stacks.emplace(stack_key, ValueAndCount{}).first;
  }

  PprofStacks::value_type &unique_stack = *iter;

  // Update the address map
  auto &address_map = pid_stacks._address_map;
  ValuePerAddress &v = address_map[address];

  if (v._value) {
    // Existing allocation: handle cleanup
    LG_DBG("(LIVE_ALLOC) Existing allocation found: %lx (cleaning up)",
           address);
    if (v._unique_stack) {
      // Adjust the mapping values map for the previous allocation
      pid_stacks.mapping_values[v._unique_stack->first.start_mmap]._value -=
          v._value;
      if (pid_stacks.mapping_values[v._unique_stack->first.start_mmap]._count >
          0) {
        --pid_stacks.mapping_values[v._unique_stack->first.start_mmap]._count;
      }

      v._unique_stack->second._value -= v._value;
      if (v._unique_stack->second._count > 0) {
        --(v._unique_stack->second._count);
      }

      // Remove the old stack if its count is zero and not the same as the
      // current unique stack
      if (v._unique_stack != &unique_stack &&
          v._unique_stack->second._count == 0) {
        stacks.erase(v._unique_stack->first);
      }
    }
  }

  // Set the new allocation value and stack association
  v._value = value;
  v._unique_stack = &unique_stack;
  v._unique_stack->second._value += value;
  ++(v._unique_stack->second._count);

  // Update the mapping values map
  pid_stacks.mapping_values[unique_stack.first.start_mmap]._value += value;
  ++pid_stacks.mapping_values[unique_stack.first.start_mmap]._count;

  return true;
}

std::vector<SmapsEntry> LiveAllocation::parse_smaps(pid_t pid) {
  std::string smaps_file = absl::StrFormat("%s/%d/smaps", "/proc", pid);

  std::vector<SmapsEntry> entries;
  FILE *file = fopen(smaps_file.c_str(), "r");

  if (!file) {
    LG_WRN("Unable to access smaps file for %d", pid);
    return entries;
  }
  char buffer[256];
  SmapsEntry current_entry;

  while (fgets(buffer, sizeof(buffer), file)) {
    std::string_view line(buffer);

    if (line.find("Rss:") == 0) {
      // Extract RSS value (take characters after "Rss:    ")
      size_t rss = 0;
      std::string_view rss_str = line.substr(4, line.find("kB") - 4);
      rss_str.remove_prefix(std::min(rss_str.find_first_not_of(' '),
                                     rss_str.size())); // trim leading spaces
      auto res =
          std::from_chars(rss_str.data(), rss_str.data() + rss_str.size(), rss);
      if (res.ec != std::errc()) {
        LG_DBG("Failed to convert RSS value in smaps file for %d", pid);
        continue;
      }
      current_entry.rss_kb = rss;
      // push back as we are not parsing other values
      entries.push_back(std::move(current_entry));
      current_entry = SmapsEntry(); // Reset for next entry
    } else if (line.find('-') != std::string_view::npos) {
      // Extract address
      std::string_view address = line.substr(0, line.find(' '));
      size_t dash_position = address.find('-');
      unsigned long long start;
      unsigned long long end;
      // Convert start address
      auto result = std::from_chars(address.data(),
                                    address.data() + dash_position, start, 16);
      if (result.ec != std::errc()) {
        LG_DBG("Failed to convert start address in smaps file for %d", pid);
        continue;
      }
      // Convert end address
      result = std::from_chars(address.data() + dash_position + 1,
                               address.data() + address.size(), end, 16);
      if (result.ec != std::errc()) {
        LG_DBG("Failed to convert end address in smaps file for %d", pid);
        // todo skip next rss
        continue;
      }
      current_entry.end = end;
      current_entry.start = start;
    }
  }

  fclose(file);
  // should be a no-op
  std::sort(entries.begin(), entries.end(),
            [](const SmapsEntry &a, const SmapsEntry &b) {
              return a.start < b.start;
            });

  return entries;
}

int64_t
LiveAllocation::upscale_with_mapping(const PprofStacks::value_type &stack,
                                     PidStacks &pid_stacks) {
  const ValueAndCount &accounted_value =
      pid_stacks.mapping_values[stack.first.start_mmap];
  // Find the corresponding smaps entry for the given address
  auto entry =
      std::lower_bound(pid_stacks.entries.begin(), pid_stacks.entries.end(),
                       stack.first.start_mmap,
                       [](const LiveAllocation::SmapsEntry &l, uintptr_t addr) {
                         return l.start < addr;
                       });

  // We should already be matching an existing entry
  // ie. entry->start == stack.first.start_mmap
  if (entry != pid_stacks.entries.begin() &&
      (entry == pid_stacks.entries.end() ||
       stack.first.start_mmap < entry->start)) {
    --entry; // Move to the previous entry if it's not the beginning
  }

  // Check if a valid entry was found
  if (entry == pid_stacks.entries.end() ||
      (stack.first.start_mmap != entry->start)) {
    // todo: think about these cases
    LG_DBG("Unable to upscale address for mapping at %lx",
           stack.first.start_mmap);
    return 0; // or handle as needed
  }
  if (accounted_value._value == 0) {
    LG_DBG("No accounted memory for mapping at %lx", stack.first.start_mmap);
    return 0; // or handle as needed
  }

  //
  // Mapping is 10 megs of RSS
  // foo -> 10 samples / 100 kb; 10% of the memory in this mapping
  // bar -> 90 samples / 900 kb; 90% of the memory in this mapping
  //
  // foo is upscaled to 1 Meg
  // bar is upscaled to 9 megs
  //
  // Cases of interest:
  // RSS is 0 -> is that OK not to show the allocations ?
  // RSS does not shrink even if we no longer have allocations
  // -> this is where malloc stats would help us
  //
  // What if we have a different profile type to show this?
  //
  return stack.second._value * entry->rss_kb * 1000 / accounted_value._value;
}

} // namespace ddprof
