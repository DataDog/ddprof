// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddres_def.hpp"
#include "ddres_list.hpp"
#include "logger.hpp"
#include "unlikely.hpp"

#include <cstdlib>
#include <cstring>
#include <system_error>

namespace ddprof {

/// Replacement for variadic macro niladic expansion via `__VA_OPT__`, which
/// is unsupported (boo!) in standards-compliant C static analysis tools and
/// checkers.
#define DDRES_NOLOG NULL

/// Standardized way of formatting error log
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

/// Evaluate function and return error if -1 (add an error log)
#define DDRES_CHECK_ERRNO(eval, what, ...)                                     \
  do {                                                                         \
    if (unlikely((eval) == -1)) {                                              \
      const int e = errno;                                                     \
      LG_ERR(__VA_ARGS__);                                                     \
      LOG_ERROR_DETAILS(LG_ERR, what);                                         \
      LG_ERR("errno(%d): %s", e, strerror(e));                                 \
      return ddres_error(what);                                                \
    }                                                                          \
  } while (0)

/// Check boolean and log
#define DDRES_CHECK_BOOL(eval, what, ...)                                      \
  do {                                                                         \
    if (unlikely(!eval)) {                                                     \
      DDRES_RETURN_ERROR_LOG(what, __VA_ARGS__);                               \
    }                                                                          \
  } while (0)

inline int ddres_sev_to_log_level(int sev) {
  switch (sev) {
  case DD_SEV_ERROR:
    return LL_ERROR;
  case DD_SEV_WARN:
    return LL_WARNING;
  case DD_SEV_NOTICE:
    return LL_DEBUG;
  default: // no log
    return LL_LENGTH;
  }
}

/// Forward result if Fatal
#define DDRES_CHECK_FWD_STRICT(ddres)                                          \
  do {                                                                         \
    DDRes lddres = ddres; /* single eval */                                    \
    if (IsDDResNotOK(lddres)) {                                                \
      LG_IF_LVL_OK(ddres_sev_to_log_level(lddres._sev),                        \
                   "Forward error at %s:%u - %s", __FILE__, __LINE__,          \
                   ddres_error_message(lddres._what));                         \
      return lddres;                                                           \
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
      }                                                                        \
      if (lddres._sev == DD_SEV_WARN) {                                        \
        LG_WRN("Recover from sev=%d at %s:%u - %s", lddres._sev, __FILE__,     \
               __LINE__, ddres_error_message(lddres._what));                   \
      } else {                                                                 \
        LG_NTC("Recover from sev=%d at %s:%u - %s", lddres._sev, __FILE__,     \
               __LINE__, ddres_error_message(lddres._what));                   \
      }                                                                        \
    }                                                                          \
  } while (0)

/// Evaluate function and return error if -1 (add an error log)
#define DDRES_CHECK_ERRORCODE(eval, what, ...)                                 \
  do {                                                                         \
    const std::error_code err = (eval);                                        \
    if (err) {                                                                 \
      LG_ERR(__VA_ARGS__);                                                     \
      LOG_ERROR_DETAILS(LG_ERR, what);                                         \
      LG_ERR("error_code(%d): %s", err.value(), err.message().c_str());        \
      return ddres_error(what);                                                \
    }                                                                          \
  } while (0)

// ccpcheck does not support __VA_OPT__ so we need to use gcc/clang specific
// workaround ##__VA_ARGS__.
// cppcheck 2.14 will support __VA_OPT__ (cf.
// https://github.com/danmar/simplecpp/pull/329) so we can remove this
// workaround once it is released. \fixme{nsavoire}
#define DDPROF_CHECK_FATAL_IMPL(condition, condition_text, text, ...)          \
  do {                                                                         \
    if (unlikely(!(condition))) {                                              \
      LG_IF_LVL_OK(LL_CRITICAL, "Check failed: `%s`. " text, condition_text,   \
                   ##__VA_ARGS__);                                             \
      std::abort();                                                            \
    }                                                                          \
  } while (0)

#ifndef NDEBUG
#  define DDPROF_DCHECK_FATAL_IMPL(condition, condition_text, text, ...)       \
    do {                                                                       \
      if (unlikely(!(condition))) {                                            \
        LG_IF_LVL_OK(LL_CRITICAL, "Check failed: `%s`. " text, condition_text, \
                     ##__VA_ARGS__);                                           \
        std::abort();                                                          \
      }                                                                        \
    } while (0)
#else
#  define DDPROF_DCHECK_FATAL_IMPL(condition, condition_text, text, ...)       \
    do {                                                                       \
    } while (0)
#endif

#ifndef DDPROF_PROFILING_LIBRARY
// Fatal assertion check that terminates the program with a fatal error if
// `condition` is not true.
#  define DDPROF_CHECK_FATAL(condition, ...)                                   \
    DDPROF_CHECK_FATAL_IMPL(condition, #condition, __VA_ARGS__)
#else
// DDPROF_CHECK_FATAL must not be used inside profiling library
#  define DDPROF_CHECK_FATAL(condition, ...)                                   \
    static_assert(                                                             \
        false, "DDPROF_CHECK_FATAL must not be used inside profiling library")
#endif

// `DDPROF_DCHECK_FATAL` behaves like `DDPROF_CHECK_FATAL` in debug mode but
// does nothing otherwise (if NDEBUG is defined)
#define DDPROF_DCHECK_FATAL(condition, ...)                                    \
  DDPROF_DCHECK_FATAL_IMPL(condition, #condition, __VA_ARGS__)

} // namespace ddprof
