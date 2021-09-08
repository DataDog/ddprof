
#include "unwind_cache.h"

extern "C" {
#include <dwarf.h>

#include "dwfl_internals.h"
#include "logger.h"
}

#include "ddres.h"
#include "mapinfo_cache.hpp"
#include "pcinfo_cache.hpp"

#include <cassert>
#include <cstdio>

// out of namespace as these are visible on C side
// Minimal c++ structure to keep a style close to C
struct unwind_cache_hdr {
  unwind_cache_hdr() : _info_cache(), _stats(), _setting(K_CACHE_ON) {
    if (const char *env_p = std::getenv("DDPROF_CACHE_SETTING")) {
      if (strcmp(env_p, "VALIDATE") == 0) {
        // Allows to compare the accuracy of the cache
        _setting = K_CACHE_VALIDATE;
        LG_NTC("%s : Validate the cache data at every call", __FUNCTION__);
      }
    }
  }
  void display_stats() { _stats.display(); }
  ddprof::pcinfo_cache _info_cache;
  ddprof::mapinfo_hashmap _mapinfo_map;
  struct ddprof::pcinfo_cache_stats _stats;
  pcinfo_cache_setting _setting;
};

////////////////
/* C Wrappers */
////////////////

extern "C" {
DDRes pcinfo_cache_get(struct unwind_cache_hdr *cache_hdr,
                       struct Dwfl_Module *mod, Dwarf_Addr newpc, pid_t pid,
                       GElf_Off *offset, string_view *demangle_name,
                       uint32_t *lineno, string_view *srcpath) {
  try {
    string_view symname; // for error checking
    ddprof::pcinfo_cache_get(cache_hdr->_info_cache, cache_hdr->_stats, mod,
                             newpc, pid, offset, &symname, demangle_name,
                             lineno, srcpath);

    if (cache_hdr->_setting == K_CACHE_VALIDATE) {
      if (ddprof::pcinfo_cache_check(mod, newpc, *offset, symname.ptr)) {
        ++(cache_hdr->_stats._errors);
        LG_ERR("Error from ddprof::pcinfo_cache_check (hit nb %d)",
               cache_hdr->_stats._hit);
        return ddres_error(DD_WHAT_UW_CACHE_ERROR);
      }
    }
  }
  CatchExcept2DDRes();
  return ddres_init();
}

DDRes mapinfo_cache_get(struct unwind_cache_hdr *cache_hdr,
                        const Dwfl_Module *mod, string_view *sname) {
  try {
    ddprof::mapinfo_cache_get(cache_hdr->_mapinfo_map, mod, sname);
  }
  CatchExcept2DDRes();
  return ddres_init();
}

DDRes unwind_cache_hdr_clear(struct unwind_cache_hdr *cache_hdr) {
  try {
    cache_hdr->_info_cache.clear();
    cache_hdr->_mapinfo_map.clear();
    cache_hdr->_stats.display();
    cache_hdr->_stats.reset();
  }
  CatchExcept2DDRes();
  return ddres_init();
}

DDRes unwind_cache_hdr_init(struct unwind_cache_hdr **cache_hdr) {
  try {
    // considering we manipulate an opaque pointer, we need to dynamically
    // allocate the cache (in full c++ you would avoid doing this)
    *cache_hdr = new unwind_cache_hdr();
  }
  CatchExcept2DDRes();
  return ddres_init();
}

// Warning this should not throw
void unwind_cache_hdr_free(struct unwind_cache_hdr *cache_hdr) {
  try {
    if (cache_hdr) {
      cache_hdr->display_stats();
      delete cache_hdr;
    }
    // Should never throw
  } catch (...) {
    LG_ERR("Unexpected exception (code should not throw on destruction)");
    assert(false);
  }
}

} // extern C
