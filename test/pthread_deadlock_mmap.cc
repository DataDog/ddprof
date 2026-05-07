#include <cstdlib>
#include <latch>
#include <thread>

#include "dd_profiling.h"

// Test that when mmap hook is called from malloc implementation
// (without malloc hook being called because statically linked)
// the TLS state is not initialized (otherwise it would deadlock).
int main() {
  constexpr size_t kLargeAllocBytes = size_t{1024} * 1024 * 16;
  constexpr uint32_t kStopTimeoutMs = 1000;

  std::latch l(1);
  std::thread t([&] {
    l.wait();
    // large allocation to exercise the mmap hooks path.
    void *p = malloc(kLargeAllocBytes);
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
