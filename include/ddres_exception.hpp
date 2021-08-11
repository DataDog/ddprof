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
    LG_ERR("Caught bad_alloc at %s:%u", __FILE__, __LINE__);                   \
    return ddres_error(DD_WHAT_BADALLOC);                                      \
  }                                                                            \
  catch (const std::exception &e) {                                            \
    DDRes ddres = {0};                                                         \
    LG_ERR("Caught standard exception at %s:%u", __FILE__, __LINE__);          \
    return ddres_error(DD_WHAT_STDEXCEPT);                                     \
  }                                                                            \
  catch (...) {                                                                \
    LG_ERR("Caught unknown exception at %s:%u", __FILE__, __LINE__);           \
    return ddres_error(DD_WHAT_UKNWEXCEPT);                                    \
  }
