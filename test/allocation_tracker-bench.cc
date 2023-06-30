#include <benchmark/benchmark.h>

#include "allocation_tracker.hpp"
#include "ringbuffer_holder.hpp"
#include <thread>

DDPROF_NOINLINE void my_malloc(size_t size, uintptr_t addr = 0xdeadbeef) {
  ddprof::AllocationTracker::track_allocation(addr, size);
  // prevent tail call optimization
  getpid();
}

DDPROF_NOINLINE void my_free(uintptr_t addr) {
  ddprof::AllocationTracker::track_deallocation(addr);
  // prevent tail call optimization
  getpid();
}
// Function to perform allocations and deallocations
void perform_memory_operations(bool track_allocations,
                               benchmark::State &state) {
  const uint64_t rate = 1;
  const size_t buf_size_order = 5;
  ddprof::RingBufferHolder ring_buffer{buf_size_order,
                                       RingBufferType::kMPSCRingBuffer};

  if (track_allocations) {
    ddprof::AllocationTracker::allocation_tracking_init(
        rate,
        ddprof::AllocationTracker::kDeterministicSampling |
            ddprof::AllocationTracker::kTrackDeallocations,
        ring_buffer.get_buffer_info());
  }

  int nb_threads = 4;
  std::vector<std::thread> threads;
  int num_allocations = 1000;
  size_t page_size = 0x1000;
  std::random_device rd;
  std::mt19937 gen(rd());

  for (auto _ : state) {
    state.PauseTiming();

    // Initialize threads and clear addresses
    threads.clear();
    std::vector<std::vector<uintptr_t>> thread_addresses(nb_threads);

    for (int i = 0; i < nb_threads; ++i) {
      threads.emplace_back([&, i] {
        std::uniform_int_distribution<> dis(i * page_size,
                                            (i + 1) * page_size - 1);

        for (int j = 0; j < num_allocations; ++j) {
          uintptr_t addr = dis(gen);
          my_malloc(1024, addr);
          thread_addresses[i].push_back(addr);
        }
      });
    }

    for (auto &t : threads) {
      t.join();
    }

    state.ResumeTiming();

    threads.clear();
    for (int i = 0; i < nb_threads; ++i) {
      threads.emplace_back([&, i] {
        for (auto addr : thread_addresses[i]) {
          my_free(addr);
        }
      });
    }

    for (auto &t : threads) {
      t.join();
    }
  }
  ddprof::AllocationTracker::allocation_tracking_free();
}

// Benchmark without allocation tracking
static void BM_ThreadedAllocations_NoTracking(benchmark::State &state) {
  perform_memory_operations(false, state);
}

// Benchmark with allocation tracking
static void BM_ThreadedAllocations_Tracking(benchmark::State &state) {
  perform_memory_operations(true, state);
}

BENCHMARK(BM_ThreadedAllocations_NoTracking);
BENCHMARK(BM_ThreadedAllocations_Tracking);
