// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"

#include "build_id.hpp"

using Dwfl_Module = struct Dwfl_Module;

namespace ddprof {
struct DDProfModRange {
  ProcessAddress_t _low_addr = 0;
  ProcessAddress_t _high_addr = 0;
};

struct DDProfMod {
  enum Status {
    kUnknown,
    kInconsistent,
  };

  DDProfMod() = default;

  explicit DDProfMod(Status status) : _status(status) {}

  void set_build_id(std::string build_id) { _build_id = std::move(build_id); }

  // build id (string that displays the hexadecimal value)
  BuildIdStr _build_id;
  // In the current version of dwfl, the dwfl_module addresses are stable
  Dwfl_Module *_mod = nullptr;
  ProcessAddress_t _low_addr{};
  ProcessAddress_t _high_addr{};
  // The symbol bias (0 for position dependant)
  Offset_t _sym_bias{static_cast<Offset_t>(-1)};
  Status _status{kUnknown};
};

} // namespace ddprof
