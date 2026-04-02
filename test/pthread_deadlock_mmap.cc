#include <cstdlib>
#include <latch>
#include <thread>

#include "dd_profiling.h"

// Test that when mmap hook is called from malloc implementation
// (without malloc hook being called because statically linked)
// the TLS state is not initialized (otherwise it would deadlock).
int main() {
  std::latch l(1);
  std::thread t([&] {
    l.wait();
    // large allocation to exercise the mmap hooks path.
    void *p = malloc(1024 * 1024 * 16);
    free(p);
  });

  int ret = ddprof_start_profiling();
  if (ret != 0) {
    fprintf(stderr, "Failed to start profiling (ret=%d)\n", ret);
    return 1;
  }

  l.count_down();
  t.join();

  if (ret == 0) {
    ddprof_stop_profiling(1000);
  }

  return 0;
}
