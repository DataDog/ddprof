#include <benchmark/benchmark.h>

#include <thread>
#include <condition_variable>
#include <vector>
#include <mutex>
#include <atomic>
#include <random>

#include "loghandle.hpp"
#include "allocation_tracker.hpp"
#include "ringbuffer_holder.hpp"

// Global bench settings
#define LIVE_HEAP
// default rate is 524288
static constexpr uint64_t k_rate = 200000;

std::atomic<bool> reader_continue{true};

// Reader worker thread function
void read_buffer(ddprof::RingBufferHolder &holder) {
  int nb_samples = 0;
  while(reader_continue){
    ddprof::MPSCRingBufferReader reader(holder.get_ring_buffer());
    auto buf = reader.read_sample();
    if (!buf.empty()) {
      ++nb_samples;
      //      fprintf(stderr, "Yep, got sample ! \n");
    }
    std::chrono::microseconds(10000);
  }
  fprintf(stderr, "Reader thread exit, nb_samples=%d\n", nb_samples);
}

DDPROF_NOINLINE void my_malloc(size_t size, uintptr_t addr = 0xdeadbeef) {
  ddprof::AllocationTracker::track_allocation(addr, size);
  DDPROF_BLOCK_TAIL_CALL_OPTIMIZATION();
}

DDPROF_NOINLINE void my_free(uintptr_t addr) {
  ddprof::AllocationTracker::track_deallocation(addr);
  DDPROF_BLOCK_TAIL_CALL_OPTIMIZATION();
}

// Function to perform allocations and deallocations
void perform_memory_operations(bool track_allocations,
                               benchmark::State &state) {
  LogHandle handle;
  const uint64_t rate = k_rate;
  const size_t buf_size_order = 8;
#ifndef LIVE_HEAP
  uint32_t flags = ddprof::AllocationTracker::kDeterministicSampling;
#else
  uint32_t flags = ddprof::AllocationTracker::kDeterministicSampling |
      ddprof::AllocationTracker::kTrackDeallocations;
#endif

  ddprof::RingBufferHolder ring_buffer{buf_size_order,
                                       RingBufferType::kMPSCRingBuffer};

  if (track_allocations) {
    ddprof::AllocationTracker::allocation_tracking_init(
        rate, flags, k_default_perf_stack_sample_size,
        ring_buffer.get_buffer_info());
  }

  int nb_threads = 4;
  std::vector<std::thread> threads;
  int num_allocations = 1000;
  size_t page_size = 0x1000;
  std::random_device rd;
  std::mt19937 gen(rd());

  // create reader worker thread
  reader_continue = true;
  std::thread reader_thread{read_buffer, std::ref(ring_buffer)};

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

  reader_continue = false;
  reader_thread.join();

  ddprof::AllocationTracker::allocation_tracking_free();
}

// Benchmark without allocation tracking
static void BM_ShortLived_NoTracking(benchmark::State &state) {
  perform_memory_operations(false, state);
}

// Benchmark with allocation tracking
static void BM_ShortLived_Tracking(benchmark::State &state) {
  perform_memory_operations(true, state);
}

// short lived threads
BENCHMARK(BM_ShortLived_NoTracking)->MeasureProcessCPUTime()->UseRealTime();
BENCHMARK(BM_ShortLived_Tracking)->MeasureProcessCPUTime()->UseRealTime();



class WorkerThread {
public:
  std::vector<uintptr_t> addresses;
  bool allocate;

  WorkerThread() : stop(false), perform_task(false) {
    worker_thread = std::thread([this] {
      while (!stop) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this]{ return perform_task || stop; });
        if (stop)
          return;

        // Here you can perform the task for the thread
        if (allocate) {
          for (auto addr : addresses) {
            my_malloc(1024, addr);
          }
        } else {
          for (auto addr : addresses) {
            my_free(addr);
          }
        }

        perform_task = false;
      }
    });
  }

  void signal_task(bool allocate_task, const std::vector<uintptr_t>& addrs) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      addresses = addrs;
      allocate = allocate_task;
      perform_task = true;
    }
    cv.notify_one();
  }

  ~WorkerThread() {
    stop = true;
    cv.notify_one();
    if (worker_thread.joinable()) {
      worker_thread.join();
    }
  }

private:
  std::thread worker_thread;
  std::condition_variable cv;
  std::mutex mutex;
  std::atomic<bool> stop;
  std::atomic<bool> perform_task;
};

void perform_memory_operations_2(bool track_allocations,
                                benchmark::State &state) {
  LogHandle handle;
  const uint64_t rate = k_rate;
  const size_t buf_size_order = 8;
#ifndef LIVE_HEAP
  uint32_t flags = ddprof::AllocationTracker::kDeterministicSampling;
#else
    uint32_t flags = ddprof::AllocationTracker::kDeterministicSampling |
        ddprof::AllocationTracker::kTrackDeallocations;
#endif
  ddprof::RingBufferHolder ring_buffer{buf_size_order,
                                       RingBufferType::kMPSCRingBuffer};

  if (track_allocations) {
    ddprof::AllocationTracker::allocation_tracking_init(
        rate, flags, k_default_perf_stack_sample_size,
        ring_buffer.get_buffer_info());
  }

  reader_continue = true;
  std::thread reader_thread{read_buffer, std::ref(ring_buffer)};

  const int nb_threads = 4;
  std::vector<WorkerThread> workers(nb_threads);
  std::vector<std::vector<uintptr_t>> thread_addresses(nb_threads);

  int num_allocations = 1000;
  size_t page_size = 0x1000;
  std::random_device rd;
  std::mt19937 gen(rd());

  for (int i = 0; i < nb_threads; ++i) {
    std::uniform_int_distribution<> dis(i * page_size, (i + 1) * page_size - 1);
    for (int j = 0; j < num_allocations; ++j) {
      uintptr_t addr = dis(gen);
      thread_addresses[i].push_back(addr);
    }
  }

  for (auto _ : state) {
    // Allocation phase
    for (int i = 0; i < nb_threads; ++i) {
      workers[i].signal_task(true, thread_addresses[i]);
    }
    // Add delay
    std::this_thread::sleep_for(std::chrono::microseconds(100));

    // Deallocation phase
    for (int i = 0; i < nb_threads; ++i) {
      workers[i].signal_task(false, thread_addresses[i]);
    }
    // Add delay
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  reader_continue = false;
  reader_thread.join();

  ddprof::AllocationTracker::allocation_tracking_free();
}

// Benchmark without allocation tracking
static void BM_LongLived_NoTracking(benchmark::State &state) {
  perform_memory_operations_2(false, state);
}

// Benchmark with allocation tracking
static void BM_LongLived_Tracking(benchmark::State &state) {
  perform_memory_operations_2(true, state);
}

BENCHMARK(BM_LongLived_NoTracking)->MeasureProcessCPUTime();
BENCHMARK(BM_LongLived_Tracking)->MeasureProcessCPUTime();
