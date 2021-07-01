#include "llvm/Demangle/Demangle.h"

#include <cassert>
#include <cstring>

extern "C" void demangle(char* str, char* buf, size_t sz_buf) {
  assert(str);
  assert(buf);
  assert(sz_buf);
  std::string demangled = llvm::demangle(std::string(str));
  sz_buf = sz_buf > demangled.size() ? demangled.size() : sz_buf;

  memcpy(buf, demangled.c_str(), sz_buf);
}
