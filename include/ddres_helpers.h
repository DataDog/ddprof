#pragma once

#include "ddres_def.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "logger.h"

// do while idiom is used to expand in a compound statement

/// Returns a fatal ddres while using the LG_ERR API
#define DDRES_RETURN_ERROR_LOG(what, ...)                                      \
  do {                                                                         \
    LG_ERR(__VA_ARGS__);                                                       \
    return ddres_error(what);                                                  \
  } while (0)

/// Returns a warning ddres with the appropriate LG_WRN message
#define DDRES_RETURN_WARN_LOG(what, ...)                                       \
  do {                                                                         \
    LG_WRN(__VA_ARGS__);                                                       \
    return ddres_warn(what);                                                   \
  } while (0)

/// Evaluate function and return error if -1 (add an error log)
#define DDRES_CHECK_INT(eval, what, ...)                                       \
  do {                                                                         \
    if (unlikely(eval == -1)) {                                                \
      DDRES_RETURN_ERROR_LOG(what, __VA_ARGS__);                               \
    }                                                                          \
  } while (0)

/// Check boolean and log
#define DDRES_CHECK_BOOL(eval, what, ...)                                      \
  do {                                                                         \
    if (unlikely(!eval)) {                                                     \
      DDRES_RETURN_ERROR_LOG(what, __VA_ARGS__);                               \
    }                                                                          \
  } while (0)

/// Forward result if Fatal
#define DDRES_CHECK_FWD(ddres)                                                 \
  do {                                                                         \
    DDRes lddres = ddres; /* single eval */                                    \
    if (IsDDResNotOK(lddres)) {                                                \
      if (IsDDResFatal(lddres)) {                                              \
        LG_ERR("Forward error (%d) at %s:%u", lddres._what, __FILE__,          \
               __LINE__);                                                      \
        return lddres;                                                         \
      } else if (lddres._sev == DD_SEVWARN) {                                  \
        LG_WRN("Recover from sev=%d (%d) at %s:%u", lddres._sev, lddres._what, \
               __FILE__, __LINE__);                                            \
      } else {                                                                 \
        LG_NTC("Recover from sev=%d (%d) at %s:%u", lddres._sev, lddres._what, \
               __FILE__, __LINE__);                                            \
      }                                                                        \
    }                                                                          \
  } while (0)

// possible improvement : flag to consider warnings as errors

#ifdef __cplusplus
}
#endif
