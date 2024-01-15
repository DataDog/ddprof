

#pragma once

#include <span>
#include <stdint.h>

#include "perf_archmap.hpp"

namespace ap {
struct StackContext {
  const void *pc;
  uint64_t sp;
  uint64_t fp;

  void set(const void *pc, uintptr_t sp, uintptr_t fp) {
    this->pc = pc;
    this->sp = sp;
    this->fp = fp;
  }
};

// Async profiler's unwinding only uses a subset of the registers
StackContext from_regs(std::span<const uint64_t, ddprof::k_perf_register_count> regs);

struct StackBuffer {
  StackBuffer(std::span<const std::byte> bytes, uint64_t start, uint64_t end)
      : _bytes(bytes), sp_start(start), sp_end(end) {}
  std::span<const std::byte> _bytes;
  uint64_t sp_start; // initial SP (in context of the process)
  uint64_t sp_end;   // sp + size (so root functions = start of stack)
  /*
    sp_end
      For this thread, high address matches where the stack begins
      as it grows down.
    |
    Main()
    |
    FuncA()
    |
    ...
    |
    sp_start
      This matches the SP register when the stack was captured
  */
};

} // namespace ap
