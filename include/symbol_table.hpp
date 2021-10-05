#pragma once

#include "ddprof_defs.h"

#include <string>
#include <vector>

// Symbol
// Information relating to a given location
namespace ddprof {
// Value stored in the cache
class Symbol {
public:
  Symbol() : _offset(0), _lineno(0) {}

  // Warning : Generates some string copies (these are not rvalues)
  Symbol(Offset_t offset, std::string symname, std::string demangle_name,
         uint32_t lineno, std::string srcpath)
      : _offset(0), _symname(std::move(symname)),
        _demangle_name(std::move(demangle_name)), _lineno(lineno),
        _srcpath(std::move(srcpath)) {}

  // OUTPUT OF ADDRINFO
  Offset_t _offset;
  std::string _symname;

  // DEMANGLING CACHE
  std::string _demangle_name;

  // OUTPUT OF LINE INFO
  uint32_t _lineno;
  std::string _srcpath;
};

typedef std::vector<Symbol> SymbolTable;

} // namespace ddprof
