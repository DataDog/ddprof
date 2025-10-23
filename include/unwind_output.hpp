// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

// External API: This file should stay in C

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "container_id_defs.hpp"
#include "ddprof_defs.hpp"

#include "ddprof_file_info-i.hpp"

namespace ddprof {

struct FunLoc {
  ProcessAddress_t ip;
  ElfAddress_t elf_addr;
  FileInfoId_t file_info_id;
  SymbolIdx_t symbol_idx;
  MapInfoIdx_t map_info_idx;
  friend auto operator<=>(const FunLoc &, const FunLoc &) = default;
};

// NOLINTBEGIN(misc-non-private-member-variables-in-classes)
struct UnwindOutput {
  void clear() {
    locs.clear();
    container_id = k_container_id_unknown;
    exe_name = {};
    thread_name = {};
  }
  std::vector<FunLoc> locs;
  std::string_view container_id;
  std::string_view exe_name;
  std::string_view thread_name;
  int pid;
  int tid;
  friend auto operator<=>(const UnwindOutput &, const UnwindOutput &) = default;
};
// NOLINTEND(misc-non-private-member-variables-in-classes)

} // namespace ddprof
