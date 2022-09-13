// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"
#include "build_id.hpp"

#include <string>

namespace ddprof {
class MapInfo {
public:
  MapInfo() : _low_addr(0), _high_addr(0), _offset(0), _sopath() {}

  MapInfo(ElfAddress_t low_addr, ElfAddress_t high_addr, Offset_t offset,
          std::string &&sopath, BuildIdStr build_id)
      : _low_addr(low_addr), _high_addr(high_addr), _offset(offset),
        _sopath(sopath), _build_id(std::move(build_id)) {}
  ElfAddress_t _low_addr;
  ElfAddress_t _high_addr;
  Offset_t _offset;
  std::string _sopath;
  BuildIdStr _build_id;
};

typedef std::vector<MapInfo> MapInfoTable;

} // namespace ddprof
