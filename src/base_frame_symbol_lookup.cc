#include "base_frame_symbol_lookup.hpp"

#include "string_format.hpp"
extern "C" {
#include "logger.h"
}

namespace ddprof {

namespace {
Symbol symbol_from_pid(pid_t pid) {
  std::string pid_str = string_format("pid_%d", pid);
  return Symbol(0, std::string(), std::string(), 0, pid_str);
}
} // namespace

SymbolIdx_t
BaseFrameSymbolLookup::get_or_insert(pid_t pid, SymbolTable &symbol_table,
                                     DsoSymbolLookup &dso_symbol_lookup,
                                     const DsoHdr &dso_hdr) {
  auto const it = _map.find(pid);
  SymbolIdx_t symbol_idx;
  if (it != _map.end()) {
    symbol_idx = it->second;
  } else { // insert things
    symbol_idx = -1;
    if (pid) { // only when we are not on pid 0
      DsoFindRes find_res = dso_hdr.dso_find_first_executable(pid);
      if (find_res.second) {
        // use start as address as a trick to remove addr info (as it will most
        // of the time be null and ignored in the dso frame generation)
        symbol_idx = dso_symbol_lookup.get_or_insert(
            find_res.first->_start, *find_res.first, symbol_table);
      } else {
        LG_WRN("Unable to find base frame for pid %d", pid);
      }
    }
    // no dso exec dso for this pid : add a v-frame for the pid
    if (symbol_idx == -1) {
      symbol_idx = symbol_table.size();
      symbol_table.push_back(symbol_from_pid(pid));
    }
    _map.insert(std::pair<pid_t, SymbolIdx_t>(pid, symbol_idx));
  }
  return symbol_idx;
}
} // namespace ddprof
