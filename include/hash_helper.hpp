#pragma once

#include <cstddef>

namespace ddprof {

static inline std::size_t hash_combine(std::size_t lhs, std::size_t rhs) {
  return rhs + 0x9e3779b9 + (lhs << 6) + (lhs >> 2);
}

} // namespace ddprof
