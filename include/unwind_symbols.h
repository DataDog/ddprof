#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "ddprof_defs.h"
#include "ddres_def.h"
#include "string_view.h"

#include <sys/types.h>

struct UnwindSymbolsHdr;

/// Set through env var (DDPROF_CACHE_SETTING) in case of doubts on cache
typedef enum symbol_lookup_setting {
  K_CACHE_ON = 0,
  K_CACHE_VALIDATE,
} symbol_lookup_setting;

DDRes unwind_symbols_hdr_init(struct UnwindSymbolsHdr **symbols_hdr);

void unwind_symbols_hdr_free(struct UnwindSymbolsHdr *symbols_hdr);

#ifdef __cplusplus
}
#endif
