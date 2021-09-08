#include "mapinfo_cache.hpp"

extern "C" {
#include "libdwfl.h"
}

#include "ddres.h"

namespace ddprof {

static void mapinfo_link(const std::string &sname_str, string_view *sname) {
  if (sname_str.empty()) {
    sname->ptr = nullptr;
    sname->len = 0;
  } else {
    sname->ptr = sname_str.c_str();
    sname->len = 0;
  }
}

void mapinfo_cache_get(mapinfo_hashmap &mapinfo_map, const Dwfl_Module *mod,
                       string_view *sname) {

  GElf_Addr key_addr = mod->low_addr;
  auto const it = mapinfo_map.find(key_addr);
  if (it != mapinfo_map.end()) {
    mapinfo_link(it->second, sname);
  } else {
    char *localsname = strrchr(mod->name, '/');
    std::string sname_str(localsname ? localsname + 1 : mod->name);
    auto it_inser = mapinfo_map.insert(std::make_pair<GElf_Addr, std::string>(
        std::move(key_addr), std::move(sname_str)));
    mapinfo_link(it_inser.first->second, sname);
  }
}

} // namespace ddprof
