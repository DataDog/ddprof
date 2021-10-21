#pragma once

#include "ddprof_defs.h"

#include <string>

// Symbol
// Information relating to a given location
namespace ddprof {

class Symbol {
public:
  Symbol() : _lineno(0) {}

  // Warning : Generates some string copies (these are not rvalues)
  Symbol(Offset_t offset, std::string symname, std::string demangle_name,
         uint32_t lineno, std::string srcpath)
      : _symname(std::move(symname)), _demangle_name(std::move(demangle_name)),
        _lineno(lineno), _srcpath(std::move(srcpath)) {}

  // OUTPUT OF ADDRINFO
  std::string _symname;

  // DEMANGLING CACHE
  std::string _demangle_name;

  // OUTPUT OF LINE INFO
  uint32_t _lineno;
  std::string _srcpath;
};
} // namespace ddprof
