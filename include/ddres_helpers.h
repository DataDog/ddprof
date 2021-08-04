#pragma once

#include "ddres_def.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "logger.h"

// do while idiom is used to expand in a compound statement

/// Returns a fatal ddres while using the LG_ERR API
#define RETURN_FATAL_LOG(where, what, ...)                                     \
  do {                                                                         \
    LG_ERR(__VA_ARGS__);                                                       \
    return ddres_fatal(where, what);                                           \
  } while (0)

// Forward result if Fatal
#define DDERR_CHECK_FWD(ddres)                                                 \
  do {                                                                         \
    DDRes lddres = ddres;                                                      \
    if (IsDDResNotOK(lddres)) {                                                \
      if (IsDDResFatal(lddres)) {                                              \
        LG_ERR("Forward fatal (%d/%d) at %s:%u", lddres._where, lddres._what,  \
               __FILE__, __LINE__);                                            \
        return lddres;                                                         \
      } else if (lddres._sev == DD_SEVWARN) {                                  \
        LG_WRN("Recover from sev=%d (%d/%d) at %s:%u", lddres._sev,            \
               lddres._where, lddres._what, __FILE__, __LINE__);               \
      } else {                                                                 \
        LG_NTC("Recover from sev=%d (%d/%d) at %s:%u", lddres._sev,            \
               lddres._where, lddres._what, __FILE__, __LINE__);               \
      }                                                                        \
    }                                                                          \
  } while (0)

// possible improvement : flag to consider warnings as errors

#ifdef __cplusplus
}
#endif
