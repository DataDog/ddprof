// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <exception>

#include "ddres_def.hpp"
#include "ddres_helpers.hpp"
#include "ddres_list.hpp"

namespace ddprof {

/// Standard exception containing a DDRes
class DDException : public std::exception {
public:
  explicit DDException(DDRes ddres) : _ddres(ddres) {}
  DDException(int16_t sev, int16_t what) : _ddres(ddres_create(sev, what)) {}
  [[nodiscard]] DDRes get_DDRes() const { return _ddres; }

private:
  // TODO: Possibly inherit from runtime exceptions to pass string
  DDRes _ddres;
};
} // namespace ddprof

#define DDRES_THROW_EXCEPTION(what, ...)                                       \
  do {                                                                         \
    LG_ERR(__VA_ARGS__);                                                       \
    LOG_ERROR_DETAILS(LG_ERR, what);                                           \
    throw ddprof::DDException(ddres_error(what));                              \
  } while (0)

#define DDRES_CHECK_THROW_EXCEPTION(ddres)                                     \
  do {                                                                         \
    DDRes lddres = ddres; /* single eval */                                    \
    if (IsDDResNotOK(lddres)) {                                                \
      if (IsDDResFatal(lddres)) {                                              \
        LG_ERR("Forward error at %s:%u - %s", __FILE__, __LINE__,              \
               ddres_error_message(lddres._what));                             \
        throw ddprof::DDException(lddres);                                     \
      } else if (lddres._sev == DD_SEV_WARN) {                                 \
        LG_WRN("Recover from sev=%d at %s:%u - %s", lddres._sev, __FILE__,     \
               __LINE__, ddres_error_message(lddres._what));                   \
      } else {                                                                 \
        LG_NTC("Recover from sev=%d at %s:%u - %s", lddres._sev, __FILE__,     \
               __LINE__, ddres_error_message(lddres._what));                   \
      }                                                                        \
    }                                                                          \
  } while (0)

/// Catch exceptions and give info when possible
#define CatchExcept2DDRes()                                                    \
  catch (const ddprof::DDException &e) {                                       \
    DDRES_CHECK_FWD(e.get_DDRes());                                            \
  }                                                                            \
  catch (const std::bad_alloc &ba) {                                           \
    LOG_ERROR_DETAILS(LG_ERR, DD_WHAT_BADALLOC);                               \
    return ddres_error(DD_WHAT_BADALLOC);                                      \
  }                                                                            \
  catch (const std::exception &e) {                                            \
    LOG_ERROR_DETAILS(LG_ERR, DD_WHAT_STDEXCEPT);                              \
    return ddres_error(DD_WHAT_STDEXCEPT);                                     \
  }                                                                            \
  catch (...) {                                                                \
    LOG_ERROR_DETAILS(LG_ERR, DD_WHAT_UKNWEXCEPT);                             \
    return ddres_error(DD_WHAT_UKNWEXCEPT);                                    \
  }
