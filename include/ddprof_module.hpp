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

struct DDProfMod {
  enum Status {
    kUnknown,
    kInconsistent,
  };

  DDProfMod()
      : _mod(nullptr), _low_addr(0), _high_addr(0),
        _sym_bias(static_cast<Offset_t>(-1)), _status(kUnknown) {}

  explicit DDProfMod(Status status) : DDProfMod() { _status = status; }

  // In the current version of dwfl, the dwfl_module addresses are stable
  Dwfl_Module *_mod;
  ProcessAddress_t _low_addr;
  ProcessAddress_t _high_addr;
  // The symbol biais (0 for position dependant)
  Offset_t _sym_bias;
  Status _status;
};
} // namespace ddprof
