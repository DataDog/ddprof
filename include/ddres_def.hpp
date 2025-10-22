// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "unlikely.hpp"

#include <cstdint>
// although we keep it in a int16, we only need a uint8 for the enum
enum DD_RES_SEV : uint8_t {
  DD_SEV_OK = 0,
  DD_SEV_NOTICE = 1,
  DD_SEV_WARN = 2,
  DD_SEV_ERROR = 3,
};

/// Result structure containing a what / severity
struct DDRes {
  union {
    // Not Enums as I could not specify size (except with gcc or c++)
    struct {
      int16_t _what; // Type of result (define your list of results)
      int16_t _sev;  // fatal, warn, OK...
    };
    int32_t _val;
  };
};

#define FillDDRes(res, sev, what)                                              \
  do {                                                                         \
    (res)._sev = (sev);                                                        \
    (res)._what = (what);                                                      \
  } while (0)

#define InitDDResOK(res)                                                       \
  do {                                                                         \
    (res)._val = 0;                                                            \
  } while (0)

#define FillDDResFatal(res, what) FillDDRes((res), DD_SEV_ERROR, what)

/******** STANDARD APIs TO USE BELLOW **********/
// In C you should be careful of static inline vs extern inline vs inline

/// sev, what
static inline DDRes ddres_create(int16_t sev, int16_t what) {
  DDRes ddres;
  FillDDRes(ddres, sev, what);
  return ddres;
}

/// Creates a DDRes taking an error code (what)
static inline DDRes ddres_error(int16_t what) {
  return ddres_create(DD_SEV_ERROR, what);
}

/// Creates a DDRes with a warning taking an error code (what)
static inline DDRes ddres_warn(int16_t what) {
  return ddres_create(DD_SEV_WARN, what);
}

/// Create an OK DDRes
static inline DDRes ddres_init() {
  DDRes ddres = {};
  return ddres;
}

/// returns a bool : true if they are equal
static inline bool ddres_equal(DDRes lhs, DDRes rhs) {
  return lhs._val == rhs._val;
}

// Assumption behind these is that SEV_ERROR does not occur often

/// true if ddres is not OK (unlikely)
#define IsDDResNotOK(res) unlikely((res)._sev != DD_SEV_OK)

/// true if ddres is OK (likely)
#define IsDDResOK(res) likely((res)._sev == DD_SEV_OK)

/// true if ddres is fatal (unlikely)
#define IsDDResFatal(res) unlikely((res)._sev == DD_SEV_ERROR)

inline bool operator==(DDRes lhs, DDRes rhs) { return ddres_equal(lhs, rhs); }
