// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"

extern "C" {
typedef struct Dwfl_Module Dwfl_Module;
}

namespace ddprof {
struct DDProfModRange {
  ProcessAddress_t _low_addr = 0;
  ProcessAddress_t _high_addr = 0;
};

enum SymbolMethod {
  kDwarfSymbol = 0,
  kRuntimeSymbol,
};

struct DDProfMod {
  enum Status {
    kUnknown,
    kInconsistent,
  };

  DDProfMod()
      : _mod(nullptr), _low_addr(0), _high_addr(0),
        _sym_bias(static_cast<Offset_t>(-1)), _status(kUnknown),
        _symbol_method(kDwarfSymbol) {}

  explicit DDProfMod(Status status) : DDProfMod() { _status = status; }

  // In the current version of dwfl, the dwfl_module addresses are stable
  Dwfl_Module *_mod;
  ProcessAddress_t _low_addr;
  ProcessAddress_t _high_addr;
  // The symbol biais (0 for position dependant)
  Offset_t _sym_bias;
  Status _status;
  SymbolMethod _symbol_method;
};
} // namespace ddprof
