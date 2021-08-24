#pragma once

#include "ddres_def.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "ddres_list.h"
#include "logger.h"

/// Replacement for variadic macro niladic expansion via `__VA_OPT__`, which
/// is unsupported (boo!) in standards-compliant C static analysis tools and
/// checkers.
#define DDRES_NOLOG NULL

/// Standardized way of formating error log
#define LOG_ERROR_DETAILS(log_func, what)                                      \
  log_func("%s at %s:%u", ddres_error_message(what), __FILE__, __LINE__);

/// Returns a fatal ddres while using the LG_ERR API
/// To suppress printing logs, pass NULL in place of the variadic arguments or
/// DDRES_NOLOG
#define DDRES_RETURN_ERROR_LOG(what, ...)                                      \
  do {                                                                         \
    LG_ERR(__VA_ARGS__);                                                       \
    LOG_ERROR_DETAILS(LG_ERR, what);                                           \
    return ddres_error(what);                                                  \
  } while (0)

/// Returns a warning ddres with the appropriate LG_WRN message
/// The variadic arguments to log are optional
#define DDRES_RETURN_WARN_LOG(what, ...)                                       \
  do {                                                                         \
    LG_WRN(__VA_ARGS__);                                                       \
    LOG_ERROR_DETAILS(LG_WRN, what);                                           \
    return ddres_warn(what);                                                   \
  } while (0)

// Implem notes :
// 1- do while idiom is used to expand in a compound statement (if you have an
// if (A)
//   macro()
// you want to have the full content of the macro in the if statement

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
        LG_ERR("Forward error at %s:%u - %s", __FILE__, __LINE__,              \
               ddres_error_message(lddres._what));                             \
        return lddres;                                                         \
      } else if (lddres._sev == DD_SEVWARN) {                                  \
        LG_WRN("Recover from sev=%d at %s:%u - %s", lddres._sev, __FILE__,     \
               __LINE__, ddres_error_message(lddres._what));                   \
      } else {                                                                 \
        LG_NTC("Recover from sev=%d at %s:%u - %s", lddres._sev, __FILE__,     \
               __LINE__, ddres_error_message(lddres._what));                   \
      }                                                                        \
    }                                                                          \
  } while (0)

// possible improvement : flag to consider warnings as errors

#ifdef __cplusplus
}
#endif
