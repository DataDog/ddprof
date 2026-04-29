// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "mapinfo_lookup.hpp"

#include "ddog_profiling_utils.hpp"

namespace ddprof {
namespace {

bool mapping_matches(const ddog_prof_ProfilesDictionary *dict,
                     const ddog_prof_Mapping2 &mapping, const Dso &dso,
                     std::string_view filename, std::string_view build_id) {
  return mapping.memory_start == dso._start &&
      mapping.memory_limit == dso._end && mapping.file_offset == dso._offset &&
      get_string(dict, mapping.filename) == filename &&
      get_string(dict, mapping.build_id) == build_id;
}

std::string_view basename(std::string_view path) {
  size_t const pos = path.rfind('/');
  return (pos == std::string_view::npos) ? path : path.substr(pos + 1);
}

} // namespace

MapInfoIdx_t
MapInfoLookup::get_or_insert(pid_t pid, MapInfoTable &mapinfo_table,
                             const Dso &dso,
                             const std::optional<BuildIdStr> &build_id,
                             const ddog_prof_ProfilesDictionary *dict) {
  MapInfoAddrMap &addr_map = _mapinfo_pidmap[pid];
  const std::string_view filename = basename(dso._filename);
  const std::string_view build_id_str =
      build_id ? std::string_view{*build_id} : std::string_view{};

  auto it = addr_map.find(dso._start);
  if (it != addr_map.end()) {
    const MapInfoIdx_t idx = it->second;
    if (ddog_prof_MappingId2 mapping_id = mapinfo_table[idx]; mapping_id &&
        mapping_matches(dict, *mapping_id, dso, filename, build_id_str)) {
      return idx;
    }
    // Different object remapped at the same start address: fall through to
    // allocate a new row. The old row is kept so prior indexes stay valid.
  }

  const MapInfoIdx_t map_info_idx = mapinfo_table.size();
  mapinfo_table.push_back(intern_mapping(dict, dso._start, dso._end,
                                         dso._offset, filename, build_id_str));
  addr_map[dso._start] = map_info_idx;
  return map_info_idx;
}

} // namespace ddprof
