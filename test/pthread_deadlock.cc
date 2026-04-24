#include <thread>

#include "dd_profiling.h"

namespace {
std::atomic<bool> g_stop{false};

void getattr_loop(bool loop) {
  while (!g_stop.load(std::memory_order_relaxed)) {
    pthread_attr_t attrs;
    pthread_getattr_np(pthread_self(), &attrs);
    pthread_attr_destroy(&attrs);
    if (!loop) {
      break;
    }
  }
}
} // namespace

int main() {
  // Start a thread that calls pthread_getattr_np in a tight loop
  std::thread t(getattr_loop, true);

  // Give the thread time to start running
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  // Start profiling while the thread is actively calling pthread_getattr_np
  int ret = ddprof_start_profiling();
  if (ret != 0) {
    fprintf(stderr, "Failed to start profiling (ret=%d)\n", ret);
    return 1;
  }

  // Let it run for a bit to exercise the race
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Stop the thread
  g_stop.store(true, std::memory_order_relaxed);
  t.join();

  // Test thread start
  std::thread t2(getattr_loop, false);
  t2.join();

  if (ret == 0) {
    ddprof_stop_profiling(1000);
  }

  return 0;
}
