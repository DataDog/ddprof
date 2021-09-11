#pragma once

#include "ddprof_defs.h"

#include <string>
#include <vector>

namespace ddprof {
class MapInfo {
public:
  MapInfo() : _low_addr(0), _high_addr(0), _sopath() {}
  MapInfo(ElfAddress_t low_addr, ElfAddress_t high_addr, std::string &&sopath)
      : _low_addr(0), _high_addr(0), _sopath(sopath) {}
  ElfAddress_t _low_addr;
  ElfAddress_t _high_addr;
  std::string _sopath;
};

typedef std::vector<MapInfo> MapInfoTable;

} // namespace ddprof
