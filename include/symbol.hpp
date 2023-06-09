// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"

#include <string>

// Symbol
// Information relating to a given location
namespace ddprof {

class Symbol {
public:
  Symbol() : _lineno(0) {}

  // Warning : Generates some string copies (these are not rvalues)
  Symbol(std::string symname, std::string demangle_name, uint32_t lineno,
         std::string srcpath)
      : _symname(std::move(symname)), _demangle_name(std::move(demangle_name)),
        _lineno(lineno), _srcpath(std::move(srcpath)) {}

  // OUTPUT OF ADDRINFO
  std::string _symname;

  // DEMANGLING CACHE
  std::string _demangle_name;

  // OUTPUT OF LINE INFO
  uint32_t _lineno;
  std::string _srcpath;

  bool _is_python_frame;
};
} // namespace ddprof
