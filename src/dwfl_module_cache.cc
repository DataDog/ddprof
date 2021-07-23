
#include "dwfl_module_cache.h"

extern "C" {
#include <dwarf.h>

#include "libdw.h"
#include "libdwfl.h"
#include "libebl.h"

#include "dwfl_internals.h"
#include "logger.h"
}

#include "llvm/Demangle/Demangle.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

// #define DEBUG

namespace ddprof {
// Value stored in the cache
typedef struct dwfl_addr_info {
  dwfl_addr_info() : _offset(0), _lineno(0) {}

  // OUTPUT OF ADDRINFO
  GElf_Off _offset;
  std::string _symname;

  // DEMANGLING CACHE
  std::string _demangle_name;

  // OUTPUT OF LINE INFO
  int _lineno;
  std::string _srcpath;
} dwfl_addr_info;

// Key
typedef struct dwfl_mod_pc_key {
  dwfl_mod_pc_key() : _low_addr(0), _newpc(0), _pid(-1) {}
  dwfl_mod_pc_key(const Dwfl_Module *mod, Dwarf_Addr newpc, pid_t pid)
      : _low_addr(mod->low_addr), _newpc(newpc), _pid(pid) {}

  bool operator==(const dwfl_mod_pc_key &other) const {
    return (_low_addr == other._low_addr && _newpc == other._newpc);
  }

  // Unicity on low addr : verified in single threaded environment
  GElf_Addr _low_addr;
  Dwarf_Addr _newpc;
  // Addresses are valid in the context of a pid
  pid_t _pid;
} dwfl_mod_pc_key;

// from boost
static inline std::size_t hash_combine(std::size_t lhs, std::size_t rhs) {
  return rhs + 0x9e3779b9 + (lhs << 6) + (lhs >> 2);
}
} // namespace ddprof

// Define a custom hash func for this key : needs to be in std namespace
namespace std {
template <> struct hash<ddprof::dwfl_mod_pc_key> {
  std::size_t operator()(const ddprof::dwfl_mod_pc_key &k) const {
    // Combine hashes of standard types
    std::size_t hash_val = ddprof::hash_combine(hash<GElf_Addr>()(k._low_addr),
                                                hash<Dwarf_Addr>()(k._newpc));
    hash_val = ddprof::hash_combine(hash_val, hash<int>()(k._pid));
    return hash_val;
  }
};
} // namespace std

namespace ddprof {

typedef std::unordered_map<dwfl_mod_pc_key, dwfl_addr_info>
    dwfl_addr_info_cache;

typedef std::unordered_map<GElf_Addr, std::string> sname_hashmap;

struct dwflmod_cache_stats {
  dwflmod_cache_stats() : _hit(0), _calls(0), _errors(0) {}
  void display() {
    LG_NTC("dwflmod_cache_stats : Hit / calls = [%d/%d] = %d", _hit, _calls,
           (_hit * 100) / _calls);
    LG_NTC("                   Errors / calls = [%d/%d] = %d", _errors, _calls,
           (_errors * 100) / _calls);
    // Estimate of cache size
    LG_NTC("                   Size of cache = %lu (nb el %d)",
           (_calls - _hit) * (sizeof(dwfl_addr_info) + sizeof(dwfl_mod_pc_key)),
           _calls - _hit);
  }
  int _hit;
  int _calls;
  int _errors;
};

} // namespace ddprof

// out of namespace as these are visible on C side
// Minimal c++ structure to keep a style close to C
struct dwflmod_cache_hdr {
  dwflmod_cache_hdr() : _info_cache(), _stats(), _setting(K_CACHE_ON) {
    if (const char *env_p = std::getenv("DDPROF_CACHE_SETTING")) {
      if (strcmp(env_p, "VALIDATE") == 0) {
        // Allows to compare the accuracy of the cache
        _setting = K_CACHE_VALIDATE;
        LG_NTC("%s : Validate the cache data at every call", __FUNCTION__);
      }
    }
  }
  void display_stats() { _stats.display(); }
  ddprof::dwfl_addr_info_cache _info_cache;
  ddprof::sname_hashmap _sname_map;
  struct ddprof::dwflmod_cache_stats _stats;
  dwflmod_cache_setting _setting;
};

namespace ddprof {

// Write the info from internal structure to output parameters
static void map_info(const dwfl_addr_info &info, GElf_Off *offset,
                     const char **symname, const char **demangle_name,
                     int *lineno, const char **srcpath) {

  *offset = info._offset;

  if (info._symname.empty()) {
    *symname = nullptr;
  } else {
    *symname = info._symname.c_str();
  }

  // Demangle mapping
  *demangle_name = info._demangle_name.c_str();

  // Line info mapping
  *lineno = info._lineno;
  if (info._srcpath.empty()) {
    *srcpath = nullptr;
  } else {
    *srcpath = info._srcpath.c_str();
  }
}

// compute the info using dwarf and demangle apis
static void dwfl_module_get_info(Dwfl_Module *mod, Dwarf_Addr newpc,
                                 dwfl_addr_info &info) {
  // sym not used in the rest of the process : not storing it
  GElf_Sym lsym;
  GElf_Word lshndxp;
  Elf *lelfp;
  Dwarf_Addr lbias;

  const char *lsymname = dwfl_module_addrinfo(mod, newpc, &(info._offset),
                                              &lsym, &lshndxp, &lelfp, &lbias);

  if (lsymname) {
    info._symname = std::string(lsymname);
    info._demangle_name = llvm::demangle(info._symname);
  } else {
    info._demangle_name = "0x" + std::to_string(mod->low_addr);
  }
  // The max size from libddprof export is 1023 (constant to be updated)
  if (info._demangle_name.size() > 1023) {
    info._demangle_name.resize(1023);
  }

  Dwfl_Line *line = dwfl_module_getsrc(mod, newpc);
  // srcpath
  const char *localsrcpath =
      dwfl_lineinfo(line, &newpc, &info._lineno, 0, 0, 0);
  if (localsrcpath) {
    info._srcpath = std::string(localsrcpath);
  }
}

// pass through cache
// use DDPROF_CACHE_SETTING to turn off
static void dwfl_module_cache_addrinfo(struct dwflmod_cache_hdr *cache_hdr,
                                       Dwfl_Module *mod, Dwarf_Addr newpc,
                                       pid_t pid, GElf_Off *offset,
                                       const char **symname,
                                       const char **demangle_name, int *lineno,
                                       const char **srcpath) {
  *demangle_name = NULL;
  assert(cache_hdr);
#ifdef DEBUG
  printf("DBG: associate ");
  printf("     newpc = %ld \n ", newpc);
  printf("     mod->low_addr = %ld \n ", mod->low_addr);
#endif
  ddprof::dwfl_addr_info_cache &info_cache = cache_hdr->_info_cache;

  dwfl_mod_pc_key key(mod, newpc, pid);
  auto const it = info_cache.find(key);
  ++(cache_hdr->_stats._calls);

  if (it != info_cache.end()) {
    ++(cache_hdr->_stats._hit);
#ifdef DEBUG
    printf("DBG: Found it !! \n");
#endif
    map_info(it->second, offset, symname, demangle_name, lineno, srcpath);
  } else {

#ifdef DEBUG
    printf("DBG: Did not find it !! \n");
#endif
    dwfl_addr_info info;
    dwfl_module_get_info(mod, newpc, info);

    auto it_insert =
        info_cache.insert(std::make_pair<dwfl_mod_pc_key, dwfl_addr_info>(
            std::move(key), std::move(info)));

    map_info(it_insert.first->second, offset, symname, demangle_name, lineno,
             srcpath);
  }
#ifdef DEBUG
  printf("DBG: demangled name = %s \n", *demangle_name);
  printf("     line = %d \n", *lineno);
  printf("     srcpath = %s \n", *srcpath);
  printf("     symname = %s \n ", *symname);
#endif
}

bool error_cache_values(struct Dwfl_Module *mod, Dwarf_Addr newpc,
                        GElf_Off offset, const char *symname) {

  GElf_Off loffset;
  GElf_Sym lsym;
  GElf_Word lshndxp;
  Elf *lelfp;
  Dwarf_Addr lbias;

  const char *localsymname = dwfl_module_addrinfo(mod, newpc, &loffset, &lsym,
                                                  &lshndxp, &lelfp, &lbias);
  bool error_found = false;
  if (loffset != offset) {
    LG_ERR("Error from cache offset %ld vs %ld ", loffset, offset);
    error_found = true;
  }
  if (localsymname == nullptr || symname == nullptr) {
    if (localsymname != symname)
      LG_ERR("Error from cache symname %p vs %p ", localsymname, symname);
  } else {
    if (strcmp(symname, localsymname) != 0) {
      LG_ERR("Error from cache symname %s vs %s ", localsymname, symname);
      error_found = true;
    }
    if (error_found) {
      printf("symname = %s\n", symname);
    }
  }
  return error_found;
}

void dwfl_module_cache_mapsname(const std::string &sname_str,
                                const char **sname) {
  if (sname_str.empty()) {
    *sname = nullptr;
  } else {
    *sname = sname_str.c_str();
  }
}

void dwfl_module_cache_getsname(struct dwflmod_cache_hdr *cache_hdr,
                                const Dwfl_Module *mod, const char **sname) {

  GElf_Addr key_addr = mod->low_addr;
  auto const it = cache_hdr->_sname_map.find(key_addr);
  if (it != cache_hdr->_sname_map.end()) {
    dwfl_module_cache_mapsname(it->second, sname);
  } else {
    char *localsname = strrchr(mod->name, '/');
    std::string sname_str(localsname ? localsname + 1 : mod->name);
    auto it_inser =
        cache_hdr->_sname_map.insert(std::make_pair<GElf_Addr, std::string>(
            std::move(key_addr), std::move(sname_str)));
    dwfl_module_cache_mapsname(it_inser.first->second, sname);
  }
}

} // namespace ddprof

////////////////
/* C Wrappers */
////////////////

extern "C" {
dwflmod_cache_status
dwfl_module_cache_getinfo(struct dwflmod_cache_hdr *cache_hdr,
                          struct Dwfl_Module *mod, Dwarf_Addr newpc, pid_t pid,
                          GElf_Off *offset, const char **demangle_name,
                          int *lineno, const char **srcpath) {
  try {
    const char *symname; // for error checking
    ddprof::dwfl_module_cache_addrinfo(cache_hdr, mod, newpc, pid, offset,
                                       &symname, demangle_name, lineno,
                                       srcpath);

    if (cache_hdr->_setting == K_CACHE_VALIDATE) {
      if (ddprof::error_cache_values(mod, newpc, *offset, symname)) {
        ++(cache_hdr->_stats._errors);
        LG_ERR("Error from ddprof::error_cache_values (hit nb %d)",
               cache_hdr->_stats._hit);
        return K_DWFLMOD_CACHE_KO;
      }
    }

  } catch (...) {
    LG_ERR("Error from ddprof::dwfl_module_cache_addrinfo (hit nb %d)",
           cache_hdr->_stats._hit);
    return K_DWFLMOD_CACHE_KO;
  }

  return K_DWFLMOD_CACHE_OK;
}

dwflmod_cache_status
dwfl_module_cache_getsname(struct dwflmod_cache_hdr *cache_hdr,
                           const Dwfl_Module *mod, const char **sname) {
  try {
    ddprof::dwfl_module_cache_getsname(cache_hdr, mod, sname);
  } catch (...) {
    LG_ERR("Error from ddprof::dwfl_module_cache_getsname (hit nb %d)",
           cache_hdr->_stats._hit);
    return K_DWFLMOD_CACHE_KO;
  }
  return K_DWFLMOD_CACHE_OK;
}

dwflmod_cache_status
dwflmod_cache_hdr_init(struct dwflmod_cache_hdr **cache_hdr) {
  try {
    *cache_hdr = new dwflmod_cache_hdr();
  } catch (...) { return K_DWFLMOD_CACHE_KO; }
  return K_DWFLMOD_CACHE_OK;
}

// Warning this should not throw
void dwflmod_cache_hdr_free(struct dwflmod_cache_hdr *cache_hdr) {
  try {
    if (cache_hdr) {
      cache_hdr->display_stats();
      delete cache_hdr;
    }
    // Should never throw
  } catch (...) {
    LG_ERR("Error from dwflmod_cache_hdr_free");
    assert(false);
  }
}

} // extern C
