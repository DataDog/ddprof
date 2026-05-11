// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "common_mapinfo_lookup.hpp"

#include "ddog_profiling_utils.hpp"

namespace ddprof {

MapInfoIdx_t CommonMapInfoLookup::get_or_insert(
    CommonMapInfoLookup::MappingErrors lookup_case, MapInfoTable &mapinfo_table,
    const ddog_prof_ProfilesDictionary *dict) {
  auto const it = _map.find(lookup_case);
  if (it != _map.end()) {
    return it->second;
  }
  const MapInfoIdx_t res = mapinfo_table.size();
  // Empty mapping: all-zero addresses and empty strings.
  mapinfo_table.push_back(intern_mapping(dict, 0, 0, 0, {}, {}));
  _map.insert({lookup_case, res});
  return res;
}

} // namespace ddprof
