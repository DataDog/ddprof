#include "ipinfo_lookup.hpp"

extern "C" {
#include "dwfl_internals.h"
#include "logger.h"
}

#include "llvm/Demangle/Demangle.h"

// #define DEBUG

namespace ddprof {

IPInfoKey::IPInfoKey(const Dwfl_Module *mod, ElfAddress_t newpc,
                     DsoUID_t dso_id)
    : _low_addr(mod->low_addr), _newpc(newpc), _dso_id(dso_id) {}

// compute the info using dwarf and demangle apis
static void ipinfo_get_from_dwfl(Dwfl_Module *mod, Dwarf_Addr newpc,
                                 IPInfo &info) {
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

  Dwfl_Line *line = dwfl_module_getsrc(mod, newpc);
  // srcpath
  int linep;
  const char *localsrcpath =
      dwfl_lineinfo(line, &newpc, static_cast<int *>(&linep), 0, 0, 0);
  info._lineno = static_cast<uint32_t>(linep);
  if (localsrcpath) {
    info._srcpath = std::string(localsrcpath);
  }
}

// pass through cache
void ipinfo_lookup_get(IPInfoLookup &info_cache, IPInfoLookupStats &stats,
                       IPInfoTable &table, Dwfl_Module *mod, Dwarf_Addr newpc,
                       DsoUID_t dso_id, IPInfoIdx_t *ipinfo_idx) {
  IPInfoKey key(mod, newpc, dso_id);
  auto const it = info_cache.find(key);
  ++(stats._calls);

  if (it != info_cache.end()) {
    ++(stats._hit);
#ifdef DEBUG
    printf("DBG: Cache HIT \n");
#endif
    *ipinfo_idx = it->second;
  } else {

#ifdef DEBUG
    printf("DBG: Cache MISS \n");
#endif
    IPInfo info;
    ipinfo_get_from_dwfl(mod, newpc, info);

    *ipinfo_idx = table.size();
    // This will be similar to an emplace back
    table.push_back(std::move(info));

    info_cache.insert(std::make_pair<IPInfoKey, IPInfoIdx_t>(
        std::move(key), IPInfoIdx_t(*ipinfo_idx)));
  }
#ifdef DEBUG
  printf("DBG: Func = %s  \n", table[*ipinfo_idx]._demangle_name.c_str());
  printf("     src  = %s  \n", table[*ipinfo_idx]._srcpath.c_str());
#endif
}

bool ipinfo_lookup_check(struct Dwfl_Module *mod, Dwarf_Addr newpc,
                         const IPInfo &info) {

  GElf_Off loffset;
  GElf_Sym lsym;
  GElf_Word lshndxp;
  Elf *lelfp;
  Dwarf_Addr lbias;

  const char *localsymname = dwfl_module_addrinfo(mod, newpc, &loffset, &lsym,
                                                  &lshndxp, &lelfp, &lbias);
  bool error_found = false;
  if (loffset != info._offset) {
    LG_ERR("Error from cache offset %ld vs %ld ", loffset, info._offset);
    error_found = true;
  }
  if (localsymname == nullptr || info._symname.empty()) {
    if (localsymname != nullptr)
      LG_ERR("Error from cache : non null symname = %s", localsymname);
    if (!info._symname.empty())
      LG_ERR("Error from cache : non null cache entry = %s",
             info._symname.c_str());
  } else {
    if (strcmp(info._symname.c_str(), localsymname) != 0) {
      LG_ERR("Error from cache symname %s vs %s ", localsymname,
             info._symname.c_str());
      error_found = true;
    }
    if (error_found) {
      LG_ERR("symname = %s\n", info._symname.c_str());
    }
  }
  return error_found;
}

void IPInfoLookupStats::display() {
  if (_calls) {
    LG_NTC("ipinfo_lookup_stats : Hit / calls = [%d/%d] = %d", _hit, _calls,
           (_hit * 100) / _calls);
    LG_NTC("                   Errors / calls = [%d/%d] = %d", _errors, _calls,
           (_errors * 100) / _calls);
    // Estimate of cache size
    LG_NTC("                   Size of cache = %lu (nb el %d)",
           (_calls - _hit) * (sizeof(IPInfo) + sizeof(IPInfoKey)),
           _calls - _hit);
  } else {
    LG_NTC("ipinfo_lookup_stats : 0 calls");
  }
}

void IPInfoLookupStats::reset() {
  _hit = 0;
  _calls = 0;
  _errors = 0;
}

} // namespace ddprof
