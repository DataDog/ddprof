// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

// Test binary that includes SDT probes for memory allocation tracking.
// This simulates a statically linked application with custom allocators
// that use SDT probes for profiling.
//
// Build with: g++ -o sdt_test_binary sdt_test_binary.cc
// Requires: systemtap-sdt-dev package for sys/sdt.h
//
// Verify probes with: readelf -n sdt_test_binary | grep -A4 stapsdt

#include <sys/sdt.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

// Wrapper around malloc that fires SDT probes
void *my_malloc(size_t size) {
  // Fire entry probe with size argument
  DTRACE_PROBE1(ddprof_malloc, entry, size);

  void *ptr = std::malloc(size);

  // Fire exit probe with returned pointer
  DTRACE_PROBE1(ddprof_malloc, exit, ptr);

  return ptr;
}

// Wrapper around free that fires SDT probes
void my_free(void *ptr) {
  // Fire entry probe with pointer argument
  DTRACE_PROBE1(ddprof_free, entry, ptr);

  std::free(ptr);

  // Fire exit probe (no arguments needed)
  DTRACE_PROBE(ddprof_free, exit);
}

// Wrapper around calloc
void *my_calloc(size_t nmemb, size_t size) {
  size_t total = nmemb * size;
  DTRACE_PROBE1(ddprof_malloc, entry, total);

  void *ptr = std::calloc(nmemb, size);

  DTRACE_PROBE1(ddprof_malloc, exit, ptr);

  return ptr;
}

// Wrapper around realloc
void *my_realloc(void *old_ptr, size_t size) {
  // If old_ptr is not null, this is also a free
  if (old_ptr != nullptr) {
    DTRACE_PROBE1(ddprof_free, entry, old_ptr);
    DTRACE_PROBE(ddprof_free, exit);
  }

  DTRACE_PROBE1(ddprof_malloc, entry, size);

  void *ptr = std::realloc(old_ptr, size);

  DTRACE_PROBE1(ddprof_malloc, exit, ptr);

  return ptr;
}

// Simulate some work with allocations
void do_allocations(int count, size_t base_size) {
  std::vector<void *> ptrs;
  ptrs.reserve(count);

  for (int i = 0; i < count; ++i) {
    size_t size = base_size + (i % 100) * 16;
    void *ptr = my_malloc(size);
    if (ptr) {
      std::memset(ptr, 0, size);
      ptrs.push_back(ptr);
    }
  }

  // Free half of the allocations
  for (size_t i = 0; i < ptrs.size(); i += 2) {
    my_free(ptrs[i]);
    ptrs[i] = nullptr;
  }

  // Free remaining allocations
  for (void *ptr : ptrs) {
    if (ptr) {
      my_free(ptr);
    }
  }
}

// Worker thread function
void worker_thread(int thread_id, int iterations) {
  for (int i = 0; i < iterations; ++i) {
    do_allocations(10, 64 + thread_id * 32);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

void print_usage(const char *prog) {
  std::cout << "Usage: " << prog << " [options]\n"
            << "Options:\n"
            << "  -n <count>     Number of allocation cycles (default: 100)\n"
            << "  -t <threads>   Number of worker threads (default: 2)\n"
            << "  -s <size>      Base allocation size (default: 1024)\n"
            << "  -h             Show this help\n";
}

int main(int argc, char *argv[]) {
  int cycles = 100;
  int threads = 2;
  size_t base_size = 1024;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
      cycles = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
      threads = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
      base_size = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    }
  }

  std::cout << "SDT Test Binary\n"
            << "Cycles: " << cycles << "\n"
            << "Threads: " << threads << "\n"
            << "Base size: " << base_size << "\n";

  // Do some single-threaded allocations first
  std::cout << "Starting single-threaded allocations...\n";
  do_allocations(cycles, base_size);

  // Start worker threads
  std::cout << "Starting " << threads << " worker threads...\n";
  std::vector<std::thread> workers;
  workers.reserve(threads);

  for (int i = 0; i < threads; ++i) {
    workers.emplace_back(worker_thread, i, cycles / threads);
  }

  // Wait for all threads to complete
  for (auto &t : workers) {
    t.join();
  }

  std::cout << "Done.\n";
  return 0;
}
