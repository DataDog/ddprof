// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "common_mapinfo_lookup.hpp"

namespace ddprof {

MapInfo mapinfo_from_common(CommonMapInfoLookup::LookupCases lookup_case) {
  switch (lookup_case) {
  case CommonMapInfoLookup::LookupCases::empty:
    return MapInfo();
  default:
    break;
  }
  return MapInfo();
}

MapInfoIdx_t
CommonMapInfoLookup::get_or_insert(CommonMapInfoLookup::LookupCases lookup_case,
                                   MapInfoTable &mapinfo_table) {
  auto const it = _map.find(lookup_case);
  MapInfoIdx_t res;
  if (it != _map.end()) {
    res = it->second;
  } else { // insert things
    res = mapinfo_table.size();
    mapinfo_table.push_back(mapinfo_from_common(lookup_case));
    _map.insert(std::pair<CommonMapInfoLookup::LookupCases, MapInfoIdx_t>(
        lookup_case, res));
  }
  return res;
}

} // namespace ddprof
