// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.h"
#include "mapinfo_table.hpp"

#include "dso.hpp"

#include <string>
#include <unordered_map>

struct Dwfl_Module;

namespace ddprof {

class MapInfoLookup {
public:
  MapInfoIdx_t get_or_insert(pid_t pid, MapInfoTable &mapinfo_table,
                             const Dso &dso);
  void erase(pid_t pid) {
    // table elements are not removed (TODO to gain memory usage)
    _mapinfo_pidmap.erase(pid);
  }

private:
  typedef std::unordered_map<ElfAddress_t, MapInfoIdx_t> MapInfoAddrMap;
  typedef std::unordered_map<pid_t, MapInfoAddrMap> MapInfoPidMap;

  MapInfoPidMap _mapinfo_pidmap;
};
} // namespace ddprof
