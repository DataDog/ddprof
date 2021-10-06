#pragma once

extern "C" {
#include <sys/types.h>
}
#include "dso_hdr.hpp"
#include "dso_symbol_lookup.hpp"
#include "symbol_table.hpp"

#include <unordered_map>

namespace ddprof {
class BaseFrameSymbolLookup {
public:
  SymbolIdx_t get_or_insert(pid_t pid, SymbolTable &symbol_table,
                            DsoSymbolLookup &dso_symbol_lookup,
                            const DsoHdr &dso_hdr);
  void erase(pid_t pid) { _map.erase(pid); }

private:
  std::unordered_map<pid_t, SymbolIdx_t> _map;
};

} // namespace ddprof
