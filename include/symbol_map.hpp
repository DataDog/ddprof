// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#pragma once

#include <map>

#include "ddprof_defs.hpp"

namespace ddprof {

class SymbolSpan {
public:
  SymbolSpan() : _end(0), _symbol_idx(-1) {}
  SymbolSpan(Offset_t end, SymbolIdx_t symbol_idx)
      : _end(end), _symbol_idx(symbol_idx) {}
  // push end further
  void set_end(Offset_t end) {
    if (end > _end) {
      _end = end;
    }
  }
  Offset_t get_end() const { return _end; }

  SymbolIdx_t get_symbol_idx() const { return _symbol_idx; }

private:
  // symbol end within the segment (considering file offset)
  Offset_t _end;
  // element inside internal symbol cache
  SymbolIdx_t _symbol_idx;
};

class SymbolMap : public std::map<ElfAddress_t, SymbolSpan> {
public:
  using It = SymbolMap::iterator;
  using FindRes = std::pair<SymbolMap::It, bool>;
  using ValueType =
      SymbolMap::value_type; // key value pair ElfAddress_t, SymbolSpanMap

  static bool is_within(const Offset_t &norm_pc, const ValueType &kv);
  FindRes find_closest(Offset_t norm_pc);
};

} // namespace ddprof