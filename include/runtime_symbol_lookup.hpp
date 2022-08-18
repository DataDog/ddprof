#pragma once

#include "ddprof_defs.hpp"
#include "symbol_table.hpp"

#include <map>
#include <unordered_map>

namespace ddprof {

class RumtimeSymbolVal {
public:
  RumtimeSymbolVal(ProcessAddress_t end, SymbolIdx_t symbol_idx)
      : _end(end), _symbol_idx(symbol_idx) {}
  Offset_t get_end() const { return _end; }

  SymbolIdx_t get_symbol_idx() const { return _symbol_idx; }

private:
  // symbol end within the segment (considering file offset)
  ProcessAddress_t _end;
  // element inside internal symbol cache
  SymbolIdx_t _symbol_idx;
};

class RuntimeSymbolLookup {
public:
  RuntimeSymbolLookup(std::string_view path_to_proc) : _path_to_proc(path_to_proc) {}
  SymbolIdx_t get_or_insert(pid_t pid, ProcessAddress_t pc,
                            SymbolTable &symbol_table);

private:
  using SymbolMap = std::map<ProcessAddress_t, RumtimeSymbolVal>;
  using RuntimeSymbolFindRes = std::pair<SymbolMap::iterator, bool>;
  using PidUnorderedMap = std::unordered_map<pid_t, SymbolMap>;
  void fill_perfmap_from_file(int pid, SymbolMap &symbol_map,
                              SymbolTable &symbol_table);
  FILE *perfmaps_open(int pid, const char *path_to_perfmap);

  static bool symbol_is_within(ProcessAddress_t pc,
                               const SymbolMap::value_type &kv);
  static RuntimeSymbolFindRes find_closest(SymbolMap &map, ProcessAddress_t pc);
  PidUnorderedMap _pid_map;
  std::string _path_to_proc;
};

} // namespace ddprof
