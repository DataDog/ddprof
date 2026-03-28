// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"

struct ddog_prof_Function2;

// Symbol
// Information relating to a given location
namespace ddprof {

class Symbol {
public:
  Symbol() : _lineno(0) {}

  Symbol(uint32_t lineno, ddog_prof_Function2 *function_id)
      : _lineno(lineno), _function_id(function_id) {}

  uint32_t _lineno;
  ddog_prof_Function2 *_function_id{nullptr};
};
} // namespace ddprof
