// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <benchmark/benchmark.h>

#include <atomic>
#include <random>
#include <thread>
#include <vector>

#include "address_bitset.hpp"

namespace ddprof {

// Benchmark: Single-threaded add/remove
// this is a benchmark showing when things go wrong
// Addresses being all over the place means we will create many chunks
static void BM_AddressBitset_SingleThreaded_RandomAddr(benchmark::State &state) {
  AddressBitset bitset(AddressBitset::_k_default_table_size);
  
  std::mt19937_64 gen(42);
  std::uniform_int_distribution<uintptr_t> dist(0x1000, UINTPTR_MAX);
  
  std::vector<uintptr_t> addresses;
  addresses.reserve(10000);
  for (int i = 0; i < 10000; ++i) {
    uintptr_t addr = dist(gen) & ~0xFULL; // 16-byte aligned
    addresses.push_back(addr);
  }
  
  size_t idx = 0;
  for (auto _ : state) {
    uintptr_t addr = addresses[idx % addresses.size()];
    
    // Add then remove
    bitset.add(addr);
    benchmark::DoNotOptimize(bitset.remove(addr));
    
    ++idx;
  }
  
  state.SetItemsProcessed(state.iterations() * 2); // 2 ops per iteration
}
BENCHMARK(BM_AddressBitset_SingleThreaded_RandomAddr);

// Benchmark: Multi-threaded stress test with realistic pattern
// Each thread simulates allocation/deallocation patterns
static void BM_AddressBitset_MultiThreaded(benchmark::State &state) {
  static AddressBitset bitset(AddressBitset::_k_default_table_size);
  
  const int thread_id = state.thread_index();
  
  // Each thread gets its own 4GB chunk (matches kChunkShift = 32)
  // This ensures each thread uses a separate hash table
  const uintptr_t base_addr = 0x100000000000ULL + 
                               (static_cast<uintptr_t>(thread_id) << 32);
  
  std::mt19937_64 gen(42 + thread_id);
  std::uniform_int_distribution<uint32_t> offset_dist(0, 0x3FFFFFFF); // Within 1GB
  
  // Pre-generate addresses for this thread
  std::vector<uintptr_t> addresses;
  addresses.reserve(1000);
  for (int i = 0; i < 1000; ++i) {
    uintptr_t addr = base_addr + (offset_dist(gen) & ~0xFULL);
    addresses.push_back(addr);
  }
  
  size_t idx = 0;
  for (auto _ : state) {
    uintptr_t addr = addresses[idx % addresses.size()];
    
    // Add then remove (simulates allocation -> deallocation)
    bitset.add(addr);
    benchmark::DoNotOptimize(bitset.remove(addr));
    
    ++idx;
  }
  
  state.SetItemsProcessed(state.iterations() * 2); // 2 ops per iteration
}
BENCHMARK(BM_AddressBitset_MultiThreaded)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8)
    ->Threads(15)
    ->Threads(20);

// Benchmark: Realistic workload - add many, remove many
// Simulates tracking live allocations
static void BM_AddressBitset_LiveTracking(benchmark::State &state) {
  static AddressBitset bitset(AddressBitset::_k_default_table_size);
  
  const int thread_id = state.thread_index();
  // Each thread gets its own 4GB chunk
  const uintptr_t base_addr = 0x100000000000ULL + 
                               (static_cast<uintptr_t>(thread_id) << 32);
  
  std::mt19937_64 gen(42 + thread_id);
  std::uniform_int_distribution<uint32_t> offset_dist(0, 0x3FFFFFFF);
  
  // Track ~1000 "live" allocations per thread
  std::vector<uintptr_t> live_addresses;
  live_addresses.reserve(1000);
  
  for (auto _ : state) {
    // Add new allocation
    uintptr_t new_addr = base_addr + (offset_dist(gen) & ~0xFULL);
    if (bitset.add(new_addr)) {
      live_addresses.push_back(new_addr);
    }
    
    // Periodically remove old allocations
    if (live_addresses.size() > 900) {
      size_t to_remove = live_addresses.size() / 10;
      for (size_t i = 0; i < to_remove; ++i) {
        bitset.remove(live_addresses[i]);
      }
      live_addresses.erase(live_addresses.begin(), 
                          live_addresses.begin() + to_remove);
    }
    
    benchmark::DoNotOptimize(live_addresses.size());
  }
  
  // Cleanup
  for (auto addr : live_addresses) {
    bitset.remove(addr);
  }
  
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddressBitset_LiveTracking)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8)
    ->Threads(15);

// Benchmark: High contention - same address range
static void BM_AddressBitset_HighContention(benchmark::State &state) {
  static AddressBitset bitset(AddressBitset::_k_default_table_size);
  
  // All threads compete for same address space
  std::mt19937_64 gen(42 + state.thread_index());
  std::uniform_int_distribution<uint32_t> offset_dist(0, 0xFFFF);
  const uintptr_t base_addr = 0x7f0000000000ULL;
  
  for (auto _ : state) {
    uintptr_t addr = base_addr + ((offset_dist(gen) << 4) & 0xFFFFF0);
    
    bitset.add(addr);
    benchmark::DoNotOptimize(bitset.remove(addr));
  }
  
  state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_AddressBitset_HighContention)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8)
    ->Threads(15);

// Benchmark: 400M events on 15 threads (your target)
// This measures total time to process 400M events
static void BM_AddressBitset_400M_Events(benchmark::State &state) {
  static AddressBitset bitset(AddressBitset::_k_default_table_size);
  
  const int thread_id = state.thread_index();
  // Each thread gets its own 4GB chunk
  const uintptr_t base_addr = 0x100000000000ULL + 
                               (static_cast<uintptr_t>(thread_id) << 32);
  
  std::mt19937_64 gen(42 + thread_id);
  std::uniform_int_distribution<uint32_t> offset_dist(0, 0x3FFFFFFF);
  
  // Generate address pool
  std::vector<uintptr_t> addresses;
  addresses.reserve(10000);
  for (int i = 0; i < 10000; ++i) {
    uintptr_t addr = base_addr + (offset_dist(gen) & ~0xFULL);
    addresses.push_back(addr);
  }
  
  // Each thread processes fixed number of events
  // 400M events / 15 threads / 2 (add+remove) = 13.33M iterations per thread
  constexpr uint64_t kIterationsPerThread = 400'000'000 / 15 / 2;
  
  uint64_t iterations = 0;
  size_t idx = 0;
  
  for (auto _ : state) {
    // Do a batch of operations
    for (int i = 0; i < 1000 && iterations < kIterationsPerThread; ++i) {
      uintptr_t addr = addresses[idx % addresses.size()];
      
      bitset.add(addr);
      bitset.remove(addr);
      
      ++idx;
      ++iterations;
    }
    
    if (iterations >= kIterationsPerThread) {
      break;
    }
  }
  
  state.SetItemsProcessed(iterations * 2); // 2 ops per iteration
  state.counters["ThreadEvents"] = iterations * 2;
}
BENCHMARK(BM_AddressBitset_400M_Events)
    ->Threads(15)
    ->Unit(benchmark::kSecond)
    ->Iterations(1);

// ============================================================================
// REALISTIC MALLOC-LIKE BENCHMARKS
// Real malloc addresses are NOT random - they are clustered and sequential
// ============================================================================

// Simulate real allocator behavior: sequential addresses with occasional reuse
static void BM_AddressBitset_ReallocPattern(benchmark::State &state) {
  static AddressBitset bitset(AddressBitset::_k_default_table_size);
  
  const int thread_id = state.thread_index();
  
  // Each thread gets its own 4GB chunk (like real malloc per-thread arenas)
  const uintptr_t arena_start = 0x100000000000ULL + 
                                 (static_cast<uintptr_t>(thread_id) << 32);
  
  // Simulate bump allocator: sequential addresses
  uintptr_t current_addr = arena_start;
  constexpr size_t kTypicalAllocSize = 64; // Average allocation size
  
  // Keep pool of "freed" addresses for reuse (like real malloc)
  std::vector<uintptr_t> free_list;
  free_list.reserve(100);
  
  std::mt19937 gen(42 + thread_id);
  std::uniform_int_distribution<int> reuse_dist(0, 100);
  
  for (auto _ : state) {
    uintptr_t addr;
    
    // 30% chance to reuse freed address (simulates free/malloc pattern)
    if (!free_list.empty() && reuse_dist(gen) < 30) {
      addr = free_list.back();
      free_list.pop_back();
      bitset.add(addr); // Reallocate
    } else {
      // New allocation: bump pointer
      addr = current_addr;
      current_addr += kTypicalAllocSize;
      
      if (bitset.add(addr)) {
        // Successfully tracked, later "free" it
        free_list.push_back(addr);
      }
    }
    
    // Sometimes free an address
    if (!free_list.empty() && reuse_dist(gen) < 40) {
      uintptr_t to_free = free_list.back();
      free_list.pop_back();
      benchmark::DoNotOptimize(bitset.remove(to_free));
    }
  }
  
  // Cleanup
  for (auto addr : free_list) {
    bitset.remove(addr);
  }
  
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddressBitset_ReallocPattern)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8)
    ->Threads(15);

// Benchmark: Dense sequential allocations (like array allocations)
static void BM_AddressBitset_DenseSequential(benchmark::State &state) {
  static AddressBitset bitset(AddressBitset::_k_default_table_size);
  
  const int thread_id = state.thread_index();
  
  // Each thread gets its own 4GB chunk
  const uintptr_t base = 0x100000000000ULL + 
                          (static_cast<uintptr_t>(thread_id) << 32);
  
  uintptr_t addr = base;
  constexpr uintptr_t kAllocGranularity = 32; // Dense allocations
  
  for (auto _ : state) {
    // Sequential allocation
    bitset.add(addr);
    benchmark::DoNotOptimize(bitset.remove(addr));
    
    addr += kAllocGranularity;
    
    // Wrap around in region to avoid running out of space
    if ((addr - base) > 0x10000000) { // 256MB region
      addr = base;
    }
  }
  
  state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_AddressBitset_DenseSequential)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8)
    ->Threads(15);

// Benchmark: Page-aligned allocations (mmap-like)
// These are larger jumps but still clustered
static void BM_AddressBitset_PageAligned(benchmark::State &state) {
  static AddressBitset bitset(AddressBitset::_k_default_table_size);
  
  const int thread_id = state.thread_index();
  
  // Each thread gets its own 4GB chunk
  const uintptr_t base = 0x100000000000ULL + 
                          (static_cast<uintptr_t>(thread_id) << 32);
  
  uintptr_t addr = base;
  constexpr uintptr_t kPageSize = 4096;
  
  for (auto _ : state) {
    bitset.add(addr);
    benchmark::DoNotOptimize(bitset.remove(addr));
    
    addr += kPageSize;
    
    // Wrap around
    if ((addr - base) > 0x40000000) { // 1GB region
      addr = base;
    }
  }
  
  state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_AddressBitset_PageAligned)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8)
    ->Threads(15);

// Benchmark: Mixed allocation sizes (more realistic)
// Small, medium, large allocations from different size classes
static void BM_AddressBitset_MixedSizes(benchmark::State &state) {
  static AddressBitset bitset(AddressBitset::_k_default_table_size);
  
  const int thread_id = state.thread_index();
  
  // Three different regions for different size classes (like jemalloc)
  // Each region separated by enough space to avoid chunk conflicts
  const uintptr_t small_region = 0x100000000000ULL + 
                                  (static_cast<uintptr_t>(thread_id) << 32);
  const uintptr_t medium_region = 0x200000000000ULL + 
                                   (static_cast<uintptr_t>(thread_id) << 32);
  const uintptr_t large_region = 0x300000000000ULL + 
                                  (static_cast<uintptr_t>(thread_id) << 32);
  
  uintptr_t small_addr = small_region;
  uintptr_t medium_addr = medium_region;
  uintptr_t large_addr = large_region;
  
  std::mt19937 gen(42 + thread_id);
  std::uniform_int_distribution<int> size_class(0, 99);
  
  for (auto _ : state) {
    int choice = size_class(gen);
    uintptr_t addr;
    
    if (choice < 70) {
      // 70% small allocations (16-256 bytes)
      addr = small_addr;
      small_addr += 64;
    } else if (choice < 95) {
      // 25% medium allocations (256-4K bytes)
      addr = medium_addr;
      medium_addr += 1024;
    } else {
      // 5% large allocations (>4K bytes)
      addr = large_addr;
      large_addr += 8192;
    }
    
    bitset.add(addr);
    benchmark::DoNotOptimize(bitset.remove(addr));
  }
  
  state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_AddressBitset_MixedSizes)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8)
    ->Threads(15);

// ============================================================================
// FREE LOOKUP BENCHMARKS (Most Critical!)
// In production, most free() calls are on NON-TRACKED addresses
// ============================================================================

// Benchmark: Lookup speed for addresses NOT in the table (common case for free)
static void BM_AddressBitset_FreeLookupMiss(benchmark::State &state) {
  static AddressBitset bitset(AddressBitset::_k_default_table_size);
  
  const int thread_id = state.thread_index();
  const uintptr_t base_addr = 0x100000000000ULL + 
                               (static_cast<uintptr_t>(thread_id) << 32);
  
  // Pre-populate with some addresses (10% of address space)
  std::mt19937_64 gen(42 + thread_id);
  std::uniform_int_distribution<uint32_t> offset_dist(0, 0x3FFFFFFF);
  
  for (int i = 0; i < 1000; ++i) {
    uintptr_t addr = base_addr + (offset_dist(gen) & ~0xFULL);
    bitset.add(addr);
  }
  
  // Now test lookup on DIFFERENT addresses (not in table)
  std::mt19937_64 test_gen(1000 + thread_id);
  std::vector<uintptr_t> test_addresses;
  test_addresses.reserve(10000);
  for (int i = 0; i < 10000; ++i) {
    uintptr_t addr = base_addr + (test_gen() & 0x3FFFFFF0ULL);
    test_addresses.push_back(addr);
  }
  
  size_t idx = 0;
  for (auto _ : state) {
    uintptr_t addr = test_addresses[idx % test_addresses.size()];
    
    // This should almost always be a miss (address not in table)
    benchmark::DoNotOptimize(bitset.remove(addr));
    
    ++idx;
  }
  
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddressBitset_FreeLookupMiss)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8)
    ->Threads(15);

// Benchmark: Mixed hit/miss ratio (more realistic)
// Simulates: 20% tracked allocations, 80% untracked
static void BM_AddressBitset_FreeMixedHitRate(benchmark::State &state) {
  static AddressBitset bitset(AddressBitset::_k_default_table_size);
  
  const int thread_id = state.thread_index();
  const uintptr_t base_addr = 0x100000000000ULL + 
                               (static_cast<uintptr_t>(thread_id) << 32);
  
  std::mt19937_64 gen(42 + thread_id);
  std::uniform_int_distribution<uint32_t> offset_dist(0, 0x3FFFFFFF);
  
  // Build address pool: 20% tracked, 80% untracked
  std::vector<uintptr_t> tracked_addrs;
  std::vector<uintptr_t> untracked_addrs;
  tracked_addrs.reserve(2000);
  untracked_addrs.reserve(8000);
  
  for (int i = 0; i < 10000; ++i) {
    uintptr_t addr = base_addr + (offset_dist(gen) & ~0xFULL);
    if (i < 2000) {
      bitset.add(addr);
      tracked_addrs.push_back(addr);
    } else {
      untracked_addrs.push_back(addr);
    }
  }
  
  std::uniform_int_distribution<int> hit_dist(0, 99);
  size_t tracked_idx = 0;
  size_t untracked_idx = 0;
  
  for (auto _ : state) {
    uintptr_t addr;
    
    // 20% chance of hitting tracked address
    if (hit_dist(gen) < 20) {
      addr = tracked_addrs[tracked_idx % tracked_addrs.size()];
      ++tracked_idx;
    } else {
      addr = untracked_addrs[untracked_idx % untracked_addrs.size()];
      ++untracked_idx;
    }
    
    benchmark::DoNotOptimize(bitset.remove(addr));
  }
  
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddressBitset_FreeMixedHitRate)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8)
    ->Threads(15);

// Benchmark: Worst case - lookup miss with NO table allocated for chunk
// This tests the "early return" path when chunk table doesn't exist
static void BM_AddressBitset_FreeEmptyChunk(benchmark::State &state) {
  static AddressBitset bitset(AddressBitset::_k_default_table_size);
  
  const int thread_id = state.thread_index();
  
  // Use chunks that will NEVER have tables allocated
  // Start way beyond any populated chunks
  const uintptr_t base_addr = 0x500000000000ULL + 
                               (static_cast<uintptr_t>(thread_id) << 32);
  
  std::mt19937_64 gen(42 + thread_id);
  std::uniform_int_distribution<uint32_t> offset_dist(0, 0x3FFFFFFF);
  
  std::vector<uintptr_t> addresses;
  addresses.reserve(1000);
  for (int i = 0; i < 1000; ++i) {
    uintptr_t addr = base_addr + (offset_dist(gen) & ~0xFULL);
    addresses.push_back(addr);
  }
  
  size_t idx = 0;
  for (auto _ : state) {
    uintptr_t addr = addresses[idx % addresses.size()];
    
    // Should return immediately (no table for this chunk)
    benchmark::DoNotOptimize(bitset.remove(addr));
    
    ++idx;
  }
  
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddressBitset_FreeEmptyChunk)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8)
    ->Threads(15);

} // namespace ddprof

BENCHMARK_MAIN();

