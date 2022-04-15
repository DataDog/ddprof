// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "perf_map_symbol_lookup.hpp"

#include "dso_type.hpp"
#include "string_format.hpp"
#include "symbol_table.hpp"

extern "C" {
#include "logger.h"
}

namespace ddprof {

  bool PerfMapSymbolLookup::is_perfmap(ProcessAddress_t addr, pid_t pid) {
    std::string line;
  }

  std::ifstream *PerfMapSymbolLookup::get_perfmap_handle(pid_t pid) {
    auto &fs = perfmap_handles.find(pid);

    if (fs != perfmap_handles.end()) {
      // If a PID does not have a perfmap, we assume for now that it never will.
      // This is a bad assumption. TODO
      std::string perfmap_path = "/tmp/perf-" + std::to_string(pid) + ".map";
      auto p = perfmap_handles.emplace(std::pair<
          pid,
          std::ifstream(perfmap_path)>);
      fs = p.second();
    }
    return fs;
  }

  bool PerfMapSymbolLookup::find_in_cache_or_perfmap(ProcessAddress_t addr, pid_t pid) {
    // This is slightly wasteful, but whenever we check for a perfmap entry, we
    // need to validate that the perfmap hasn't changed.  The only way to do that
    // is to check the size vs cursor of the underlying file.
    // That means we need to check the file first.  There is a strong assumption
    // that if we're requesting an anonymous region, then the perfmap exists
    // and contains the information for the symbol now, if it's ever going to.
    std::ifstream &fs = get_perfmap_handle(pid);
    if (fs.bad())
      return false;

    // Try to drain whatever we have.  This amounts to checking that the symbol
    // has or hasn't been updated.
    std::string line;
    while (std::getline(fs, line)) {
      // Three things might happen.
      // 1.  We have a new interval which does not overlap.  Add it.
      // 2.  We exactly hit an existing interval, in which case return it
      // 3.  We intersect one or more existing intervals.  Invalidate + add.
      size_t i1 = line.find(' ');
      size_t i2 = i1 + 1 + line.substr(i1+1).find(' ');
      unsigned long addr_start = std::stoul(line, nullptr, 16);
      unsigned long addr_sz = std::stoul(line.substr(i1+1), nullptr, 16);
    }
  }

//SymbolIdx_t PerfMapSymbolLookup::get_or_insert(ElfAddress_t addr,
//                                               const Dso &dso
//                                               SymbolTable &symbol_table) {
//}

}
