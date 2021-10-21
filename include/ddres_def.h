#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "unlikely.h"

#include <stdbool.h>
#include <stdint.h>

#define DD_SEVOK 0
#define DD_SEVNOTICE 1
#define DD_SEVWARN 2
#define DD_SEVERROR 3

/// Result structure containing a what / severity
typedef struct DDRes {
  union {
    // Not Enums as I could not specify size (except with gcc or c++)
    struct {
      int16_t _what; // Type of result (define your lisqt of results)
      int16_t _sev;  // fatal, warn, OK...
    };
    int32_t _val;
  };
} DDRes;

#define FillDDRes(res, sev, what)                                              \
  do {                                                                         \
    (res)._sev = (sev);                                                        \
    (res)._what = (what);                                                      \
  } while (0)

#define InitDDResOK(res)                                                       \
  do {                                                                         \
    (res)._val = 0;                                                            \
  } while (0)

#define FillDDResFatal(res, what) FillDDRes((res), DD_SEVERROR, what)

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
  return ddres_create(DD_SEVERROR, what);
}

/// Creates a DDRes with a warning taking an error code (what)
static inline DDRes ddres_warn(int16_t what) {
  return ddres_create(DD_SEVWARN, what);
}

/// Create an OK DDRes
static inline DDRes ddres_init(void) {
  DDRes ddres = {0};
  return ddres;
}

/// returns a bool : true if they are equal
static inline bool ddres_equal(DDRes lhs, DDRes rhs) {
  return lhs._val == rhs._val;
}

// Assumption behind these is that SEVERROR does not occur often

/// true if ddres is not OK (unlikely)
#define IsDDResNotOK(res) unlikely((res)._sev != DD_SEVOK)

/// true if ddres is OK (likely)
#define IsDDResOK(res) likely((res)._sev == DD_SEVOK)

/// true if ddres is fatal (unlikely)
#define IsDDResFatal(res) unlikely((res)._sev == DD_SEVERROR)

#ifdef __cplusplus
} // extern "C"

inline bool operator==(DDRes lhs, DDRes rhs) { return ddres_equal(lhs, rhs); }
#endif
