#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <limits.h>

#define DD_ERNO_START_RANGE 1000
#define DD_NATIVE_START_RANGE 2000

// generic erno errors available from /usr/include/asm-generic/errno.h

enum DDRes_What {
  // erno starts after ELAST 106 as of now
  DD_WHAT_MIN_ERNO = DD_ERNO_START_RANGE,
  // common errors
  DD_WHAT_UKNW, // favour explicit errorsq
  DD_WHAT_BADALLOC,
  DD_WHAT_STDEXCEPT,
  DD_WHAT_UKNWEXCEPT,
  DD_WHAT_MIN_NATIVE = DD_NATIVE_START_RANGE,
  DD_WHAT_DWFL_LIB_ERROR, // external lib error
  DD_WHAT_UW_CACHE_ERROR,
  DD_WHAT_CAPLIB,
  // native errors
  DD_WHAT_UNITTEST,
  // max
  DD_WHAT_MAX = SHRT_MAX,
};

#ifdef __cplusplus
}
#endif
