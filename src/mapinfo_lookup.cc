#include "mapinfo_lookup.hpp"

extern "C" {
#include "dwfl_internals.h"
#include <dwarf.h>
}

#include "ddres.h"

namespace ddprof {

void mapinfo_lookup_get(MapInfoLookup &mapinfo_map, MapInfoTable &mapinfo_table,
                        const Dwfl_Module *mod, MapInfoIdx_t *map_info_idx) {

  GElf_Addr key_addr = mod->low_addr;
  auto const it = mapinfo_map.find(key_addr);
  if (it != mapinfo_map.end()) {
    *map_info_idx = it->second;
  } else {
    char *localsname = strrchr(mod->name, '/');
    std::string sname_str(localsname ? localsname + 1 : mod->name);
    *map_info_idx = mapinfo_table.size();

    mapinfo_table.emplace_back(mod->low_addr, mod->high_addr,
                               std::move(sname_str));
    mapinfo_map.insert(std::make_pair<ElfAddress_t, MapInfoIdx_t>(
        std::move(key_addr), MapInfoIdx_t(*map_info_idx)));
  }
}

} // namespace ddprof
