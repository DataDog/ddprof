// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "dso.hpp"
#include "hash_helper.hpp"
#include "symbol_table.hpp"

#include <unordered_map>

namespace ddprof {

class DsoSymbolLookup {
public:
  SymbolIdx_t get_or_insert(ElfAddress_t addr, const Dso &dso,
                            SymbolTable &symbol_table);

  // only binary info
  SymbolIdx_t get_or_insert(const Dso &dso, SymbolTable &symbol_table);

private:
  SymbolIdx_t get_or_insert_unhandled_type(const Dso &dso,
                                           SymbolTable &symbol_table);
  // map of maps --> the aim is to monitor usage of some maps and clear them
  // toghether
  // TODO : find efficient clear on symbol table before we do this
  typedef std::unordered_map<FileAddress_t, SymbolIdx_t> AddrDwflSymbolLookup;
  typedef std::unordered_map<std::string, AddrDwflSymbolLookup>
      DsoDwflSymbolLookup;
  DsoDwflSymbolLookup _map_dso;
  // For non-standard DSO types, address is not relevant
  std::unordered_map<dso::DsoType, SymbolIdx_t, EnumClassHash>
      _map_unhandled_dso;
};

} // namespace ddprof
