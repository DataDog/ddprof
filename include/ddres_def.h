#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <ddprof/unlikely.h>
#include <stdint.h>

#define DD_SEVOK 0
#define DD_SEVNOTICE 1
#define DD_SEVWARN 2
#define DD_SEVERROR 3

/// Result structure containing a what / where / severity
typedef struct DDRes {
  union {
    // Not Enums as I could not specify size (except with gcc or c++)
    struct {
      int32_t _what;  // Type of result (define your lisqt of results)
      int16_t _where; // Module ID / where did it happen
      int16_t _sev;   // fatal, warn, OK...
    };
    int64_t _val;
  };
} DDRes;

#define FillDDRes(res, sev, where, what)                                       \
  do {                                                                         \
    res._sev = sev;                                                            \
    res._where = where;                                                        \
    res._what = what;                                                          \
  } while (0)

#define InitDDResOK(res)                                                       \
  do {                                                                         \
    res._val = 0;                                                              \
  } while (0)

#define FillDDResFatal(res, where, what)                                       \
  FillDDRes(res, DD_SEVERROR, where, what)

/******** STANDARD APIs TO USE BELLOW **********/

/// sev, where, what (in that order ! no relevant type checking)
inline DDRes ddres_create(int16_t sev, int16_t where, int32_t what) {
  DDRes ddres;
  FillDDRes(ddres, sev, where, what);
  return ddres;
}

/// where, what (in that order)
inline DDRes ddres_fatal(int16_t where, int32_t what) {
  return ddres_create(DD_SEVERROR, where, what);
}

/// create an empty ddres with an OK sev
inline DDRes ddres_init(void) {
  DDRes ddres = {0};
  return ddres;
}

/// returns a bool : true if they are equal
inline bool ddres_equal(DDRes lhs, DDRes rhs) { return lhs._val == rhs._val; }

// Assumption behind these is that SEVERROR does not occur often

/// true if ddres is not OK (unlikely)
#define IsDDResNotOK(res) unlikely(res._sev != DD_SEVOK)

/// true if ddres is OK (likely)
#define IsDDResOK(res) likely(res._sev == DD_SEVOK)

/// true if ddres is fatal (unlikely)
#define IsDDResFatal(res) unlikely(res._sev == DD_SEVERROR)

#ifdef __cplusplus
}

inline bool operator==(DDRes lhs, DDRes rhs) { return ddres_equal(lhs, rhs); }
#endif
