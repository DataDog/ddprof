// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <exception>

#include "ddres_def.h"
#include "ddres_helpers.h"
#include "ddres_list.h"

namespace ddprof {

/// Standard exception containing a DDRes
class DDException : public std::exception {
public:
  explicit DDException(DDRes ddres) : _ddres(ddres) {}
  DDException(int16_t sev, int16_t what) : _ddres(ddres_create(sev, what)) {}
  DDRes get_DDRes() const { return _ddres; }

private:
  // TODO: Possibly inherit from runtime exceptions to pass string
  const DDRes _ddres;
};
} // namespace ddprof

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
