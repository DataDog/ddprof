// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

// External API: This file should stay in C

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "container_id_defs.hpp"
#include "ddprof_defs.hpp"

typedef struct FunLoc {
  uint64_t ip; // Relative to file, not VMA
  SymbolIdx_t _symbol_idx;
  MapInfoIdx_t _map_info_idx;

  auto operator<=>(const FunLoc &) const = default;
} FunLoc;

struct UnwindOutput {
  void clear() {
    locs.clear();
    is_incomplete = true;
    container_id = k_container_id_unknown;
  }
  std::vector<FunLoc> locs;
  std::string container_id;
  int pid;
  int tid;
  bool is_incomplete;

  auto operator<=>(const UnwindOutput &) const = default;
};
