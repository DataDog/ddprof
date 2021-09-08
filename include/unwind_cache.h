#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "ddres_def.h"
#include "libdw.h"
#include "string_view.h"

struct Dwfl_Module;
struct unwind_cache_hdr;

typedef int32_t mod_info_idx_t;

/// Set through env var (DDPROF_CACHE_SETTING) in case of doubts on cache
typedef enum pcinfo_cache_setting {
  K_CACHE_ON = 0,
  K_CACHE_VALIDATE,
} pcinfo_cache_setting;

DDRes unwind_cache_hdr_init(struct unwind_cache_hdr **cache_hdr);

void unwind_cache_hdr_free(struct unwind_cache_hdr *cache_hdr);

DDRes unwind_cache_hdr_clear(struct unwind_cache_hdr *cache_hdr);

// Takes a dwarf module and an instruction pointer, returns associated symbols
// Checks in cache to see if there is already this symbol name
DDRes pcinfo_cache_get(struct unwind_cache_hdr *cache_hdr,
                       struct Dwfl_Module *mod, Dwarf_Addr newpc, pid_t pid,
                       GElf_Off *offset, string_view *symname, uint32_t *lineno,
                       string_view *srcpath);

DDRes mapinfo_cache_get(struct unwind_cache_hdr *cache_hdr,
                        const struct Dwfl_Module *mod, string_view *sname);

#ifdef __cplusplus
}
#endif
