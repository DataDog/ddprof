#include "mapinfo_lookup.hpp"

#include "ddres.h"

namespace ddprof {

MapInfoIdx_t MapInfoLookup::get_or_insert(pid_t pid,
                                          MapInfoTable &mapinfo_table,
                                          const Dso &dso) {
  MapInfoAddrMap &addr_map = _mapinfo_pidmap[pid];
  auto it = addr_map.find(dso._start);

  if (it == addr_map.end()) { // create a mapinfo from dso element
    size_t pos = dso._filename.rfind('/');
    std::string sname_str = (pos == std::string::npos)
        ? dso._filename
        : dso._filename.substr(pos + 1);
    MapInfoIdx_t map_info_idx = mapinfo_table.size();
    mapinfo_table.emplace_back(dso._start, dso._end, dso._pgoff,
                               std::move(sname_str));
    addr_map.emplace(dso._start, map_info_idx);
    return map_info_idx;
  } else {
    return it->second;
  }
}

} // namespace ddprof
