#include "llvm/Demangle/Demangle.h"

#include <cassert>
#include <cstring>

extern "C" void demangle(const char *str, char *buf, size_t sz_buf) {
  assert(str);
  assert(buf);
  assert(sz_buf > 5); // it's way too tricky if we can't support truncation
  std::string demangled = llvm::demangle(std::string(str));

  // When the user says `sz_buf`, take that to mean the real size of the buffer.
  // We need to track a NULL, so bump it down
  --sz_buf;

  // If a symbol name hits the limit of the provided buffer, then there's no
  // easy way for downstream consumers to know whether a name has been
  // truncated (this is valuable in C++ and Rust if the caller is surfacing
  // type information).  It's too cumbersome to assume the caller will have a
  // great way of telling its own downstream consumers (except by an equivalent
  // mechanism as we're using here) about truncation, so we mutate the truncated
  // return to be transparently illegal in the underlying language (as far as I
  // know, any language which supports infix operators must exclude them from
  // identifiers).  Accordingly, truncate and end with `---` in that case.
  if (sz_buf > demangled.size()) {
    memcpy(buf, demangled.c_str(), demangled.size());
    buf[demangled.size()] = '\0';
  } else {
    memcpy(buf, demangled.c_str(), sz_buf);
    buf[sz_buf] = '\0';
    buf[sz_buf - 1] = '-';
    buf[sz_buf - 2] = '-';
    buf[sz_buf - 3] = '-';
  }
}
