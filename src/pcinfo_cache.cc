#include "pcinfo_cache.hpp"

extern "C" {
#include "logger.h"
}

#include "llvm/Demangle/Demangle.h"

// #define DEBUG

namespace ddprof {

// Write the info from internal structure to output parameters
static void pcinfo_link(const pcinfo &info, GElf_Off *offset,
                        string_view *symname, string_view *demangle_name,
                        uint32_t *lineno, string_view *srcpath) {

  *offset = info._offset;

  if (info._symname.empty()) {
    symname->ptr = nullptr;
    symname->len = 0;
  } else {
    symname->ptr = info._symname.c_str();
    symname->len = info._symname.length();
  }

  // Demangle mapping
  demangle_name->ptr = info._demangle_name.c_str();
  demangle_name->len = info._demangle_name.length();

  // Line info mapping
  *lineno = info._lineno;
  if (info._srcpath.empty()) {
    srcpath->ptr = nullptr;
    srcpath->len = 0;

  } else {
    srcpath->ptr = info._srcpath.c_str();
    srcpath->len = info._srcpath.length();
  }
}

// compute the info using dwarf and demangle apis
static void pcinfo_get_from_dwfl(Dwfl_Module *mod, Dwarf_Addr newpc,
                                 pcinfo &info) {
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
void pcinfo_cache_get(pcinfo_cache &info_cache, pcinfo_cache_stats &stats,
                      Dwfl_Module *mod, Dwarf_Addr newpc, pid_t pid,
                      GElf_Off *offset, string_view *symname,
                      string_view *demangle_name, uint32_t *lineno,
                      string_view *srcpath) {
#ifdef DEBUG
  printf("DBG: associate ");
  printf("     newpc = %ld \n ", newpc);
  printf("     mod->low_addr = %ld \n ", mod->low_addr);
#endif
  pcinfo_key key(mod, newpc, pid);
  auto const it = info_cache.find(key);
  ++(stats._calls);

  if (it != info_cache.end()) {
    ++(stats._hit);
#ifdef DEBUG
    printf("DBG: Cache HIT \n");
#endif
    pcinfo_link(it->second, offset, symname, demangle_name, lineno, srcpath);
  } else {

#ifdef DEBUG
    printf("DBG: Cache MISS \n");
#endif
    pcinfo info;
    pcinfo_get_from_dwfl(mod, newpc, info);

    auto it_insert = info_cache.insert(
        std::make_pair<pcinfo_key, pcinfo>(std::move(key), std::move(info)));

    pcinfo_link(it_insert.first->second, offset, symname, demangle_name, lineno,
                srcpath);
  }
#ifdef DEBUG
  printf("DBG: demangled name = %s \n", demangle_name->ptr);
  printf("     line = %u \n", *lineno);
  printf("     srcpath = %s \n", srcpath->ptr);
  printf("     symname = %s \n ", symname->ptr);
#endif
}

bool pcinfo_cache_check(struct Dwfl_Module *mod, Dwarf_Addr newpc,
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

void pcinfo_cache_stats::display() {
  if (_calls) {
    LG_NTC("pcinfo_cache_stats : Hit / calls = [%d/%d] = %d", _hit, _calls,
           (_hit * 100) / _calls);
    LG_NTC("                   Errors / calls = [%d/%d] = %d", _errors, _calls,
           (_errors * 100) / _calls);
    // Estimate of cache size
    LG_NTC("                   Size of cache = %lu (nb el %d)",
           (_calls - _hit) * (sizeof(pcinfo) + sizeof(pcinfo_key)),
           _calls - _hit);
  } else {
    LG_NTC("pcinfo_cache_stats : 0 calls");
  }
}

void pcinfo_cache_stats::reset() {
  _hit = 0;
  _calls = 0;
  _errors = 0;
}

} // namespace ddprof
