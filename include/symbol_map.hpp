// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#pragma once

#include <map>

#include "ddprof_defs.hpp"
#include <logger.hpp>

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
  [[nodiscard]] Offset_t get_end() const { return _end; }

  [[nodiscard]] SymbolIdx_t get_symbol_idx() const { return _symbol_idx; }

private:
  // symbol end within the segment (considering file offset)
  Offset_t _end;
  // element inside internal symbol cache
  SymbolIdx_t _symbol_idx;
};



class SymbolMap : private std::map<ElfAddress_t, SymbolSpan> {
public:
  using Map = std::map<ElfAddress_t, SymbolSpan>;
  using It = Map::iterator;
  using ConstIt = Map::const_iterator;
  using FindRes = std::pair<It, bool>;
  using ValueType =
      Map::value_type; // key value pair ElfAddress_t, SymbolSpanMap
  // Functions we forward from underlying map type

  using Map::begin;
  using Map::clear;
  using Map::emplace;
  using Map::emplace_hint;
  using Map::empty;
  using Map::end;
  using Map::erase;
  using Map::size;

  static bool is_within(const Offset_t &norm_pc, const ValueType &kv);
  FindRes find_closest(Offset_t norm_pc);
};


class NestedSymbolSpan {
public:
  NestedSymbolSpan() : _end(0), _symbol_idx(-1), _parent_addr(0) {}
  NestedSymbolSpan(Offset_t end, SymbolIdx_t symbol_idx,
                   ElfAddress_t parent = 0)
      : _end(end), _symbol_idx(symbol_idx), _parent_addr(parent) {}
  void set_end(Offset_t end) {
    if (end > _end) {
      _end = end;
    }
  }
  [[nodiscard]] Offset_t get_end() const { return _end; }
  [[nodiscard]] SymbolIdx_t get_symbol_idx() const { return _symbol_idx; }
  [[nodiscard]] ElfAddress_t get_parent_addr() const{
    return _parent_addr;
  }

private:
  Offset_t _end;
  SymbolIdx_t _symbol_idx;
  ElfAddress_t _parent_addr;
};
class NestedSymbolMap : private std::map<ElfAddress_t, NestedSymbolSpan> {
public:
  using Map = std::map<ElfAddress_t, NestedSymbolSpan>;
  using It = Map::iterator;
  using ConstIt = Map::const_iterator;
  using FindRes = std::pair<It, bool>;
  using ValueType = Map::value_type;
  using Map::begin;
  using Map::clear;
  using Map::emplace;
  using Map::emplace_hint;
  using Map::empty;
  using Map::end;
  using Map::erase;
  using Map::size;

  FindRes find_closest(Offset_t norm_pc);

  static bool is_within(const Offset_t &norm_pc, const ValueType &kv);
};

} // namespace ddprof
