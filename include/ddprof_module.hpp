// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"

#include "build_id.hpp"

extern "C" {
typedef struct Dwfl_Module Dwfl_Module;
}

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

  DDProfMod()
      : _mod(nullptr), _low_addr(0), _high_addr(0),
        _sym_bias(static_cast<Offset_t>(-1)), _status(kUnknown) {}

  explicit DDProfMod(Status status) : DDProfMod() { _status = status; }

  void set_build_id(BuildIdSpan x) { _build_id = format_build_id(x); }

  // build id (string that displays the hexadecimal value)
  BuildIdStr _build_id;
  // In the current version of dwfl, the dwfl_module addresses are stable
  Dwfl_Module *_mod;
  ProcessAddress_t _low_addr;
  ProcessAddress_t _high_addr;
  // The symbol biais (0 for position dependant)
  Offset_t _sym_bias;
  Status _status;
};

} // namespace ddprof
