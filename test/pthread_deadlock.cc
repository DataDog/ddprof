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
  using namespace std::chrono_literals;
  constexpr auto kStartupDelay = 10ms;
  constexpr auto kRaceWindow = 100ms;
  constexpr uint32_t kStopTimeoutMs = 1000;

  // Start a thread that calls pthread_getattr_np in a tight loop
  std::thread t(getattr_loop, true);

  // Give the thread time to start running
  std::this_thread::sleep_for(kStartupDelay);

  // Start profiling while the thread is actively calling pthread_getattr_np
  int const ret = ddprof_start_profiling();
  if (ret != 0) {
    fprintf(stderr, "Failed to start profiling (ret=%d)\n", ret);
    return 1;
  }

  // Let it run for a bit to exercise the race
  std::this_thread::sleep_for(kRaceWindow);

  // Stop the thread
  g_stop.store(true, std::memory_order_relaxed);
  t.join();

  // Test thread start
  std::thread t2(getattr_loop, false);
  t2.join();

  ddprof_stop_profiling(kStopTimeoutMs);

  return 0;
}
