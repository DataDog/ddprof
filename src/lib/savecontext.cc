// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "savecontext.hpp"

#include "ddprof_base.hpp"
#include "defer.hpp"
#include "pthread_fixes.hpp"
#include "saveregisters.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

namespace ddprof {

// Returns an empty span in case of failure
// Fills start (low address, so closer to SP) and end (stack end address is the
// start of the stack since stack grows down)
DDPROF_NOINLINE std::span<const std::byte> retrieve_stack_bounds() {
  void *stack_addr;
  size_t stack_size;
  pthread_attr_t attrs;
  if (pthread_getattr_np_safe(pthread_self(), &attrs) != 0) {
    return {};
  }
  defer { pthread_attr_destroy(&attrs); };
  if (pthread_attr_getstack(&attrs, &stack_addr, &stack_size) != 0) {
    return {};
  }
  return {static_cast<std::byte *>(stack_addr),
          static_cast<std::byte *>(stack_addr) + stack_size};
}

namespace {
constexpr uintptr_t hex_digit(char c) {
  return c <= '9' ? c - '0' : c - 'a' + 10;
}
} // namespace

DDPROF_NOINLINE std::span<const std::byte> retrieve_stack_bounds_from_maps() {
  const auto sp = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
  const int fd = ::open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return {};
  }
  defer { ::close(fd); };

  // Each line starts with "<start>-<end> " in lowercase hex; only that prefix
  // is parsed, the rest of the line is skipped. Streaming over a fixed stack
  // buffer avoids any heap allocation.
  enum class Field : uint8_t { kStart, kEnd, kRestOfLine };
  Field field = Field::kStart;
  uintptr_t start = 0;
  uintptr_t end = 0;
  char buf[4096];
  ssize_t n;
  while ((n = ::read(fd, buf, sizeof(buf))) != 0) {
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return {};
    }
    for (const char c : std::span{buf, static_cast<size_t>(n)}) {
      if (c == '\n') {
        field = Field::kStart;
        start = end = 0;
      } else if (field == Field::kStart) {
        if (c == '-') {
          field = Field::kEnd;
        } else {
          start = (start << 4) | hex_digit(c);
        }
      } else if (field == Field::kEnd) {
        if (c == ' ') {
          if (start <= sp && sp < end) {
            return {reinterpret_cast<const std::byte *>(start),
                    reinterpret_cast<const std::byte *>(end)};
          }
          field = Field::kRestOfLine;
        } else {
          end = (end << 4) | hex_digit(c);
        }
      }
    }
  }
  return {};
}

namespace {
// Disable address sanitizer, otherwise it will report a stack-buffer-underflow
// when we are grabbing the stack. But this is not enough, because ASAN
// intercepts memcpy and reports a stack underflow there, empirically it appears
// that both attributes and a suppression are required.
DDPROF_NO_SANITIZER_ADDRESS size_t
save_stack(std::span<const std::byte> stack_bounds, const std::byte *stack_ptr,
           std::span<std::byte> buffer) {
  // Safety check to ensure we are not in a fiber using a different stack
  if (stack_ptr < to_address(stack_bounds.begin()) ||
      stack_ptr >= to_address(stack_bounds.end())) {
    return 0;
  }
  // take the min of current stack size and requested stack sample size
  int64_t const saved_stack_size =
      std::min(static_cast<intptr_t>(buffer.size()),
               to_address(stack_bounds.end()) - stack_ptr);

  if (saved_stack_size <= 0) {
    return 0;
  }
  // Use memmove to stay on the safe side in case caller has put `buffer` on the
  // stack
  memmove(buffer.data(), stack_ptr, saved_stack_size);
  return saved_stack_size;
}
} // namespace

size_t save_context(std::span<const std::byte> stack_bounds,
                    std::span<uint64_t, k_perf_register_count> regs,
                    std::span<std::byte> buffer) {
  save_registers(regs);
  // save the stack just after saving registers, stack part above saved SP
  // must not be changed between call to save_registers and call to save_stack
  return save_stack(stack_bounds,
                    reinterpret_cast<const std::byte *>(regs[REGNAME(SP)]),
                    buffer);
}

} // namespace ddprof
