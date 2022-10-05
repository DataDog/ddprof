#pragma once

#include "ddprof_defs.hpp"
#include "logger.hpp"
#include "unwind_output.hpp"

#include <unordered_map>

namespace ddprof {

class SystemAllocation {
private:
  template <class T> T to_page(T a) {
    return ((a + T{4095ull}) & (~T{4095ull})) >> T{12ull};
  }

public:
  void add_allocs(const UnwindOutput &stack, uintptr_t addr, size_t size,
                  pid_t pid) {
    StackMap &stack_map = _pid_map[pid];

    // Convert addr to page idx, then page-align size and decimate
    uintptr_t page_start = to_page(addr);
    uintptr_t page_end = to_page(addr + size);

    for (auto i = page_start; i <= page_end; ++i) {
      stack_map[i] = stack;
    }
  }

  void move_allocs(uintptr_t addr0, uintptr_t addr1, size_t size, pid_t pid) {
    StackMap &stack_map = _pid_map[pid];

    // Convert addr to page idx
    uintptr_t page_start_0 = to_page(addr0);
    uintptr_t page_end_0 = to_page(addr0 + size);
    uintptr_t page_start_1 = to_page(addr1);
    uintptr_t page_idx_max = page_end_0 - page_start_0;

    // Can ranges overlap?  Better not try to delete them all at end...
    for (uintptr_t i = 0; i < page_idx_max; ++i) {
      stack_map[page_start_1 + i] = stack_map[page_start_0 + i];
      stack_map.erase(page_start_0 + i);
    }
  }

  void del_allocs(uintptr_t addr, size_t size, pid_t pid) {
    StackMap &stack_map = _pid_map[pid];

    // Convert addr to page idx, then page-align size and decimate
    uintptr_t page_start = to_page(addr);
    uintptr_t page_end = to_page(addr + size);

    for (auto i = page_start; i <= page_end; ++i) {
      stack_map.erase(i);
    }
  }

  void do_mmap(const UnwindOutput &stack, uintptr_t addr, size_t size,
               pid_t pid) {
    add_allocs(stack, addr, size, pid);
  }

  void do_munmap(uintptr_t addr, size_t size, pid_t pid) {
    del_allocs(addr, size, pid);
  }

  void do_madvise(uintptr_t addr, size_t size, int flags, pid_t pid) {
    // No reason to worry about this yet, since it only has to do with RSS
  }

  void do_mremap(const UnwindOutput &stack, uintptr_t addr0, uintptr_t addr1,
                 size_t size0, size_t size1, pid_t pid) {
    // We could either classify these pages as belonging to the original mmap
    // or to the mremap.  We chose the latter for now.
    // Note that we potentially duplicate a lot of work here in the case
    // that addr0 == addr1
    del_allocs(addr0, size0, pid);
    add_allocs(stack, addr1, size1, pid);
  }

  using StackMap = std::unordered_map<uintptr_t, UnwindOutput>;
  using PidMap = std::unordered_map<pid_t, StackMap>;

  PidMap _pid_map;
  int watcher_pos;
};

} // namespace ddprof
