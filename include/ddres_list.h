#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <limits.h>

enum DDRes_Where {
  DD_LOC_UKNW = 0,
  DD_LOC_UNWIND = 1,
  DD_LOC_UW_CACHE = 2,
  DD_LOC_UNITTEST = 3,
  // max
  DD_LOC_MAX = SHRT_MAX,
};

#define DD_ERNO_START_RANGE 1000
#define DD_NATIVE_START_RANGE 2000

enum DDRes_What {
  // erno starts after ELAST 106 as of now
  DD_WHAT_MIN_ERNO = DD_ERNO_START_RANGE,
  // common errors
  DD_WHAT_UKNW, // favour explicit errorsq
  DD_WHAT_BADALLOC,
  DD_WHAT_STDEXCEPT,
  DD_WHAT_UKNWEXCEPT,
  DD_WHAT_MIN_NATIVE = DD_NATIVE_START_RANGE,
  // native errors
  DD_WHAT_UNITTEST,
  // max
  DD_WHAT_MAX = INT_MAX,
};

#ifdef __cplusplus
}
#endif
