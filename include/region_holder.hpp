// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

extern "C" {
#include "ddprof_defs.h"
}
#include <string>
#include <unordered_map>

#include "ddprof_file_info-i.hpp"
#include "dso_type.hpp"
#include "hash_helper.hpp"

namespace ddprof {

// mmaps the given regions
class RegionHolder {
public:
  RegionHolder();
  RegionHolder(const std::string &full_path, size_t sz, uint64_t pgoff,
               dso::DsoType path_type);
  ~RegionHolder();
  RegionHolder(RegionHolder &&other) : RegionHolder() { swap(*this, other); }

  RegionHolder &operator=(RegionHolder &&other) {
    swap(*this, other);
    return *this;
  }

  RegionHolder(const RegionHolder &other) = delete;            // avoid copy
  RegionHolder &operator=(const RegionHolder &other) = delete; // avoid copy
  void *get_region() const { return _region; }
  std::size_t get_sz() const { return _sz; }

private:
  static void swap(RegionHolder &first, RegionHolder &second) noexcept {
    std::swap(first._region, second._region);
    std::swap(first._sz, second._sz);
    std::swap(first._type, second._type);
  }

  void *_region;
  std::size_t _sz;
  dso::DsoType _type;
};

// Associate files to mmaped regions
typedef std::unordered_map<FileInfoId_t, RegionHolder> RegionMap;

} // namespace ddprof