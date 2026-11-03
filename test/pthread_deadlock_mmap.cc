#include <cstdlib>
#include <latch>
#include <thread>

#include "dd_profiling.h"

// Exercise lazy TLS initialization when a statically linked allocator calls
// mmap. The worker is created before profiling, so its pthread start is not
// hooked.
int main() {
  constexpr size_t kLargeAllocBytes = size_t{1024} * 1024 * 16;
  constexpr uint32_t kStopTimeoutMs = 1000;

  std::latch l(1);
  std::thread t([&] {
    l.wait();
    // large allocation to exercise the mmap hooks path.
    void *p = malloc(kLargeAllocBytes);
    if (!p) {
      std::abort();
    }
    *static_cast<volatile char *>(p) = 0;
    free(p);
  });

  int const ret = ddprof_start_profiling();
  if (ret != 0) {
    fprintf(stderr, "Failed to start profiling (ret=%d)\n", ret);
    return 1;
  }

  l.count_down();
  t.join();

  ddprof_stop_profiling(kStopTimeoutMs);

  return 0;
}
