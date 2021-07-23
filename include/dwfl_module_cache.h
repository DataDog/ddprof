#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "libdw.h"

struct dwflmod_cache_hdr;

typedef enum dwflmod_cache_status {
  K_DWFLMOD_CACHE_OK,
  K_DWFLMOD_CACHE_KO,
} dwflmod_cache_status;

typedef enum dwflmod_cache_setting {
  K_CACHE_ON = 0,
  K_CACHE_VALIDATE,
} dwflmod_cache_setting;

dwflmod_cache_status
dwflmod_cache_hdr_init(struct dwflmod_cache_hdr **cache_hdr);

void dwflmod_cache_hdr_free(struct dwflmod_cache_hdr *cache_hdr);

// Takes a dwarf module and an instruction pointer, returns associated symbols
// Checks in cache to see if there is already this symbol name
// Returns K_DWFLMOD_CACHE_OK if the process ran OK
dwflmod_cache_status
dwfl_module_cache_getinfo(struct dwflmod_cache_hdr *cache_hdr,
                          struct Dwfl_Module *mod, Dwarf_Addr newpc, pid_t pid,
                          GElf_Off *offset, const char **symname, int *lineno,
                          const char **srcpath);

dwflmod_cache_status
dwfl_module_cache_getsname(struct dwflmod_cache_hdr *cache_hdr,
                           const Dwfl_Module *mod, const char **sname);

#ifdef __cplusplus
}
#endif
