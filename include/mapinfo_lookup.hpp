// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "build_id.hpp"
#include "ddprof_defs.hpp"
#include "dso.hpp"
#include "mapinfo_table.hpp"

#include <optional>
#include <string>
#include <unordered_map>

struct ddog_prof_ProfilesDictionary;

namespace ddprof {

class MapInfoLookup {
public:
  MapInfoIdx_t get_or_insert(pid_t pid, MapInfoTable &mapinfo_table,
                             const Dso &dso,
                             const std::optional<BuildIdStr> &build_id,
                             const ddog_prof_ProfilesDictionary *dict);
  void erase(pid_t pid) {
    // table elements are not removed so prior MapInfoIdx_t values stay valid.
    _mapinfo_pidmap.erase(pid);
  }

private:
  using MapInfoAddrMap = std::unordered_map<ElfAddress_t, MapInfoIdx_t>;
  using MapInfoPidMap = std::unordered_map<pid_t, MapInfoAddrMap>;

  MapInfoPidMap _mapinfo_pidmap;
};
} // namespace ddprof
