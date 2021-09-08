#pragma once
extern "C" {
#include "dwfl_internals.h"

#include "string_view.h"
}

#include <string>
#include <unordered_map>

namespace ddprof {
// Value stored in the cache
typedef struct pcinfo {
  pcinfo() : _offset(0), _lineno(0) {}

  // OUTPUT OF ADDRINFO
  GElf_Off _offset;
  std::string _symname;

  // DEMANGLING CACHE
  std::string _demangle_name;

  // OUTPUT OF LINE INFO
  uint32_t _lineno;
  std::string _srcpath;
} pcinfo;

// Key
typedef struct pcinfo_key {
  pcinfo_key() : _low_addr(0), _newpc(0), _pid(-1) {}
  pcinfo_key(const Dwfl_Module *mod, Dwarf_Addr newpc, pid_t pid)
      : _low_addr(mod->low_addr), _newpc(newpc), _pid(pid) {}

  bool operator==(const pcinfo_key &other) const {
    return (_low_addr == other._low_addr && _newpc == other._newpc);
  }

  // Unicity on low addr : verified in single threaded environment
  GElf_Addr _low_addr;
  Dwarf_Addr _newpc;
  // Addresses are valid in the context of a pid
  pid_t _pid;
} pcinfo_key;

static inline std::size_t hash_combine(std::size_t lhs, std::size_t rhs) {
  return rhs + 0x9e3779b9 + (lhs << 6) + (lhs >> 2);
}

} // namespace ddprof

// Define a custom hash func for this key :
// - needs to be in std namespace to be automatically picked up by std container
namespace std {
template <> struct hash<ddprof::pcinfo_key> {
  std::size_t operator()(const ddprof::pcinfo_key &k) const {
    // Combine hashes of standard types
    std::size_t hash_val = ddprof::hash_combine(hash<GElf_Addr>()(k._low_addr),
                                                hash<Dwarf_Addr>()(k._newpc));
    hash_val = ddprof::hash_combine(hash_val, hash<int>()(k._pid));
    return hash_val;
  }
};
} // namespace std

namespace ddprof {
typedef std::unordered_map<pcinfo_key, pcinfo> pcinfo_cache;

struct pcinfo_cache_stats {
  pcinfo_cache_stats() : _hit(0), _calls(0), _errors(0) {}
  void reset();
  void display();
  int _hit;
  int _calls;
  int _errors;
};

///// PUBLIC FUNCTIONS //////
void pcinfo_cache_get(pcinfo_cache &info_cache, pcinfo_cache_stats &stats,
                      Dwfl_Module *mod, Dwarf_Addr newpc, pid_t pid,
                      GElf_Off *offset, string_view *symname,
                      string_view *demangle_name, uint32_t *lineno,
                      string_view *srcpath);

bool pcinfo_cache_check(struct Dwfl_Module *mod, Dwarf_Addr newpc,
                        GElf_Off offset, const char *symname);

} // namespace ddprof
