#pragma once

extern "C" {
#include "string_view.h"
}

#include "ddprof_defs.h"
#include "dso.hpp"
#include "hash_helper.hpp"
#include "symbol_table.hpp"

#include <unordered_map>

struct Dwfl_Module;

namespace ddprof {

// Key
typedef struct DwflSymbolKey {
  DwflSymbolKey(const Dwfl_Module *mod, ElfAddress_t newpc, DsoUID_t dso_id);

  bool operator==(const DwflSymbolKey &other) const {
    return (_low_addr == other._low_addr && _newpc == other._newpc);
  }

  // Unicity on low addr : verified in single threaded environment
  ElfAddress_t _low_addr;
  ElfAddress_t _newpc;
  // Addresses are valid in the context of a pid
  DsoUID_t _dso_id;
} DwflSymbolKey;

} // namespace ddprof

// Define a custom hash func for this key :
// - needs to be in std namespace to be automatically picked up by std container
namespace std {
template <> struct hash<ddprof::DwflSymbolKey> {
  std::size_t operator()(const ddprof::DwflSymbolKey &k) const {
    // Combine hashes of standard types
    std::size_t hash_val = ddprof::hash_combine(
        hash<ElfAddress_t>()(k._low_addr), hash<ElfAddress_t>()(k._newpc));
    hash_val = ddprof::hash_combine(hash_val, hash<DsoUID_t>()(k._dso_id));
    return hash_val;
  }
};
} // namespace std

namespace ddprof {
typedef std::unordered_map<DwflSymbolKey, SymbolIdx_t> DwflSymbolLookup;

struct DwflSymbolLookupStats {
  DwflSymbolLookupStats() : _hit(0), _calls(0), _errors(0) {}
  void reset();
  void display();
  int _hit;
  int _calls;
  int _errors;
};

///// PUBLIC FUNCTIONS /////
void dwfl_symbol_get_or_insert(DwflSymbolLookup &dwfl_symbol_lookup,
                               DwflSymbolLookupStats &stats, SymbolTable &table,
                               Dwfl_Module *mod, ElfAddress_t newpc,
                               const Dso &dso, SymbolIdx_t *symbol_idx);

bool symbol_lookup_check(struct Dwfl_Module *mod, ElfAddress_t newpc,
                         const Symbol &info);

} // namespace ddprof
