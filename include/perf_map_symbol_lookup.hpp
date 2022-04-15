// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "dso_hdr.hpp"
#include "dso_symbol_lookup.hpp"
#include "symbol_table.hpp"

// Need to retain a symbol cache
// May need to retain a map cache
// Need to retain 
//

namespace ddprof {
class PerfMapSymbolLookup {
public:
  bool addr_in_perfmap(ProcessAddress_t addr, pid_t pid);
  SymbolIdx_t get_or_insert(ProcessAddress_t addr,
                            pid_t pid,
                            SymbolTable &symbol_table);

  // Erase symbol lookup for this pid (warning symbols still exist)
//  void erase(pid_t pid) {
//    _bin_map.erase(pid);
//    _pid_map.erase(pid);
//  }
private:

  // When we check the perfmap, a couple of things happen.
  // 1.  We retain a cache of open filestreams pointing to all discovered
  //     perfmaps.  This allows us to figure out whether we've drained the
  //     file yet (i.e., is there new data)
  // 2.  When we check a perfmap, only check for addresses which are _not_ in
  //     binaries or shared libs.  At this point, we do not want to over-write
  //     other lookups
  // 3.  When overlapping symbols are discovered, it means the older symbol
  bool find_in_cache_or_perfmap(ProcessAddress_t addr, pid_t pid);

  std::ifstream *get_perfmap_handle(pid_t pid);

  std::unordered_map<pid_t, std::ifstream> perfmap_handles;
  std::unordered_map<pid_t, std::map<ProcessAddress_t, SymbolIdx_t>> perfmap;
};

} // namespace ddprof
