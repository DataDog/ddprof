#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "ddres_def.h"
#include "libdw.h"
#include "string_view.h"

struct dwflmod_cache_hdr;

/// Set through env var (DDPROF_CACHE_SETTING) in case of doubts on cache
typedef enum dwflmod_cache_setting {
  K_CACHE_ON = 0,
  K_CACHE_VALIDATE,
} dwflmod_cache_setting;

DDRes dwflmod_cache_hdr_init(struct dwflmod_cache_hdr **cache_hdr);

void dwflmod_cache_hdr_free(struct dwflmod_cache_hdr *cache_hdr);

DDRes dwflmod_cache_hdr_clear(struct dwflmod_cache_hdr *cache_hdr);

// Takes a dwarf module and an instruction pointer, returns associated symbols
// Checks in cache to see if there is already this symbol name
// Returns K_DWFLMOD_CACHE_OK if the process ran OK
DDRes dwfl_module_cache_getinfo(struct dwflmod_cache_hdr *cache_hdr,
                                struct Dwfl_Module *mod, Dwarf_Addr newpc,
                                pid_t pid, GElf_Off *offset,
                                string_view *symname, uint32_t *lineno,
                                string_view *srcpath);

DDRes dwfl_module_cache_getsname(struct dwflmod_cache_hdr *cache_hdr,
                                 const Dwfl_Module *mod, string_view *sname);

#ifdef __cplusplus
}
#endif
