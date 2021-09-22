#pragma once

extern "C" {
#include "string_view.h"
}

#include "ddprof_defs.h"
#include "hash_helper.hpp"
#include "ipinfo_table.hpp"

#include <unordered_map>

struct Dwfl_Module;

namespace ddprof {

// Key
typedef struct IPInfoKey {
  IPInfoKey() : _low_addr(0), _newpc(0), _pid(-1) {}
  IPInfoKey(const Dwfl_Module *mod, ElfAddress_t newpc, pid_t pid);

  bool operator==(const IPInfoKey &other) const {
    return (_low_addr == other._low_addr && _newpc == other._newpc);
  }

  // Unicity on low addr : verified in single threaded environment
  ElfAddress_t _low_addr;
  ElfAddress_t _newpc;
  // Addresses are valid in the context of a pid
  pid_t _pid;
} IPInfoKey;

} // namespace ddprof

// Define a custom hash func for this key :
// - needs to be in std namespace to be automatically picked up by std container
namespace std {
template <> struct hash<ddprof::IPInfoKey> {
  std::size_t operator()(const ddprof::IPInfoKey &k) const {
    // Combine hashes of standard types
    std::size_t hash_val = ddprof::hash_combine(
        hash<ElfAddress_t>()(k._low_addr), hash<ElfAddress_t>()(k._newpc));
    hash_val = ddprof::hash_combine(hash_val, hash<int>()(k._pid));
    return hash_val;
  }
};
} // namespace std

namespace ddprof {
typedef std::unordered_map<IPInfoKey, IPInfoIdx_t> IPInfoLookup;

struct IPInfoLookupStats {
  IPInfoLookupStats() : _hit(0), _calls(0), _errors(0) {}
  void reset();
  void display();
  int _hit;
  int _calls;
  int _errors;
};

///// PUBLIC FUNCTIONS /////
void ipinfo_lookup_get(IPInfoLookup &info_cache, IPInfoLookupStats &stats,
                       IPInfoTable &table, Dwfl_Module *mod, ElfAddress_t newpc,
                       pid_t pid, IPInfoIdx_t *ipinfo_idx);

bool ipinfo_lookup_check(struct Dwfl_Module *mod, ElfAddress_t newpc,
                         const IPInfo &info);

} // namespace ddprof
