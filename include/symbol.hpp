// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"

struct ddog_prof_Function2;
struct ddog_prof_StringHeader;

struct ddog_prof_Function2;

// Symbol
// Information relating to a given location
namespace ddprof {

class Symbol {
public:
  Symbol() : _lineno(0) {}

  Symbol(ddog_prof_StringHeader *name_id, ddog_prof_StringHeader *file_id,
         uint32_t lineno, ddog_prof_Function2 *function_id)
      : _name_id(name_id),
        _file_id(file_id),
        _lineno(lineno),
        _function_id(function_id) {}

  // OUTPUT OF LINE INFO
  ddog_prof_StringHeader *_name_id{nullptr};
  ddog_prof_StringHeader *_file_id{nullptr};
  uint32_t _lineno;

  ddog_prof_Function2 *_function_id{nullptr};
};
} // namespace ddprof
