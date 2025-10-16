// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <benchmark/benchmark.h>

#include "absl/container/flat_hash_set.h"
#include "absl/hash/hash.h"
#include <mutex>
#include <random>
#include <shared_mutex>
#include <vector>

#include "address_bitset.hpp"
#include "address_sampler.hpp"

namespace ddprof {

// =============================================================================
// 1. OUR OPEN ADDRESSING (signal-safe)
// =============================================================================

static void BM_OpenAddressing_MultiThreaded(benchmark::State &state) {
  static AddressBitset bitset(AddressBitset::_k_default_table_size);

  const int thread_id = state.thread_index();
  // Each thread gets its own 4GB chunk (matches kChunkShift = 32)
  const uintptr_t base_addr = 0x100000000000ULL +
                               (static_cast<uintptr_t>(thread_id) << 32);

  std::mt19937_64 gen(42 + thread_id);
  std::uniform_int_distribution<uint32_t> offset_dist(0, 0x3FFFFFFF); // Within 1GB

  for (auto _ : state) {
    uintptr_t addr = base_addr + (offset_dist(gen) & ~0xFULL);
    bitset.add(addr);
    benchmark::DoNotOptimize(bitset.remove(addr));
  }

  state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_OpenAddressing_MultiThreaded)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8)
    ->Threads(15);

// =============================================================================
// 2. STATELESS SAMPLING (signal-safe)
// =============================================================================

static void BM_StatelessSampling_MultiThreaded(benchmark::State &state) {
  AddressSampler sampler(AddressSampler::SamplingRate::EVERY_1);

  const int thread_id = state.thread_index();
  // Each thread gets its own 4GB chunk
  const uintptr_t base_addr = 0x100000000000ULL +
                               (static_cast<uintptr_t>(thread_id) << 32);

  std::mt19937_64 gen(42 + thread_id);
  std::uniform_int_distribution<uint32_t> offset_dist(0, 0x3FFFFFFF);

  for (auto _ : state) {
    uintptr_t addr = base_addr + (offset_dist(gen) & ~0xFULL);
    bool track1 = sampler.should_track(addr);
    bool track2 = sampler.should_track(addr);
    benchmark::DoNotOptimize(track1);
    benchmark::DoNotOptimize(track2);
  }

  state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_StatelessSampling_MultiThreaded)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8)
    ->Threads(15);

// =============================================================================
// 3. ABSL FLAT_HASH_SET with mutex (NOT signal-safe)
// =============================================================================

static void BM_AbslFlatHashSet_Mutex(benchmark::State &state) {
  static absl::flat_hash_set<uintptr_t> hash_set;
  static std::mutex mtx;

  const int thread_id = state.thread_index();
  // Each thread gets its own 4GB chunk
  const uintptr_t base_addr = 0x100000000000ULL +
                               (static_cast<uintptr_t>(thread_id) << 32);

  std::mt19937_64 gen(42 + thread_id);
  std::uniform_int_distribution<uint32_t> offset_dist(0, 0x3FFFFFFF);

  for (auto _ : state) {
    uintptr_t addr = base_addr + (offset_dist(gen) & ~0xFULL);

    {
      std::lock_guard<std::mutex> lock(mtx);
      hash_set.insert(addr);
    }

    {
      std::lock_guard<std::mutex> lock(mtx);
      hash_set.erase(addr);
    }
  }

  state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_AbslFlatHashSet_Mutex)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8)
    ->Threads(15);

// =============================================================================
// 4. ABSL FLAT_HASH_SET with shared_mutex (NOT signal-safe)
// =============================================================================

static void BM_AbslFlatHashSet_SharedMutex(benchmark::State &state) {
  static absl::flat_hash_set<uintptr_t> hash_set;
  static std::shared_mutex smtx;

  const int thread_id = state.thread_index();
  // Each thread gets its own 4GB chunk
  const uintptr_t base_addr = 0x100000000000ULL +
                               (static_cast<uintptr_t>(thread_id) << 32);

  std::mt19937_64 gen(42 + thread_id);
  std::uniform_int_distribution<uint32_t> offset_dist(0, 0x3FFFFFFF);

  for (auto _ : state) {
    uintptr_t addr = base_addr + (offset_dist(gen) & ~0xFULL);

    {
      std::unique_lock<std::shared_mutex> lock(smtx);
      hash_set.insert(addr);
    }

    {
      std::unique_lock<std::shared_mutex> lock(smtx);
      hash_set.erase(addr);
    }
  }

  state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_AbslFlatHashSet_SharedMutex)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8)
    ->Threads(15);

// =============================================================================
// 5. PER-THREAD ABSL (thread-local, but won't work for cross-thread free)
// =============================================================================

static void BM_AbslFlatHashSet_PerThread(benchmark::State &state) {
  thread_local absl::flat_hash_set<uintptr_t> hash_set;

  const int thread_id = state.thread_index();
  // Each thread gets its own 4GB chunk
  const uintptr_t base_addr = 0x100000000000ULL +
                               (static_cast<uintptr_t>(thread_id) << 32);

  std::mt19937_64 gen(42 + thread_id);
  std::uniform_int_distribution<uint32_t> offset_dist(0, 0x3FFFFFFF);

  for (auto _ : state) {
    uintptr_t addr = base_addr + (offset_dist(gen) & ~0xFULL);
    hash_set.insert(addr);
    hash_set.erase(addr);
  }

  state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_AbslFlatHashSet_PerThread)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8)
    ->Threads(15);

// =============================================================================
// SUMMARY COMPARISON @ 15 threads
// =============================================================================

static void BM_Summary_OpenAddressing(benchmark::State &state) {
  static AddressBitset bitset(AddressBitset::_k_default_table_size);

  const int thread_id = state.thread_index();
  // Each thread gets its own 4GB chunk
  const uintptr_t base_addr = 0x100000000000ULL +
                               (static_cast<uintptr_t>(thread_id) << 32);
  
  std::mt19937_64 gen(42 + thread_id);
  std::uniform_int_distribution<uint32_t> offset_dist(0, 0x3FFFFFFF);

  for (auto _ : state) {
    uintptr_t addr = base_addr + (offset_dist(gen) & ~0xFULL);
    bitset.add(addr);
    benchmark::DoNotOptimize(bitset.remove(addr));
  }

  state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_Summary_OpenAddressing)
    ->Threads(15)
    ->Name("1_OpenAddressing_15T_SignalSafe");

static void BM_Summary_Sampling(benchmark::State &state) {
  AddressSampler sampler(AddressSampler::SamplingRate::EVERY_1);

  const int thread_id = state.thread_index();
  // Each thread gets its own 4GB chunk
  const uintptr_t base_addr = 0x100000000000ULL +
                               (static_cast<uintptr_t>(thread_id) << 32);
  
  std::mt19937_64 gen(42 + thread_id);
  std::uniform_int_distribution<uint32_t> offset_dist(0, 0x3FFFFFFF);

  for (auto _ : state) {
    uintptr_t addr = base_addr + (offset_dist(gen) & ~0xFULL);
    bool t1 = sampler.should_track(addr);
    bool t2 = sampler.should_track(addr);
    benchmark::DoNotOptimize(t1);
    benchmark::DoNotOptimize(t2);
  }

  state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_Summary_Sampling)
    ->Threads(15)
    ->Name("2_Stateless_15T_SignalSafe");

static void BM_Summary_AbslMutex(benchmark::State &state) {
  static absl::flat_hash_set<uintptr_t> hash_set;
  static std::mutex mtx;

  const int thread_id = state.thread_index();
  // Each thread gets its own 4GB chunk
  const uintptr_t base_addr = 0x100000000000ULL +
                               (static_cast<uintptr_t>(thread_id) << 32);
  
  std::mt19937_64 gen(42 + thread_id);
  std::uniform_int_distribution<uint32_t> offset_dist(0, 0x3FFFFFFF);

  for (auto _ : state) {
    uintptr_t addr = base_addr + (offset_dist(gen) & ~0xFULL);

    {
      std::lock_guard<std::mutex> lock(mtx);
      hash_set.insert(addr);
    }
    {
      std::lock_guard<std::mutex> lock(mtx);
      hash_set.erase(addr);
    }
  }

  state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_Summary_AbslMutex)
    ->Threads(15)
    ->Name("3_Absl_Mutex_15T_NOT_SignalSafe");

static void BM_Summary_AbslPerThread(benchmark::State &state) {
  thread_local absl::flat_hash_set<uintptr_t> hash_set;

  const int thread_id = state.thread_index();
  // Each thread gets its own 4GB chunk
  const uintptr_t base_addr = 0x100000000000ULL +
                               (static_cast<uintptr_t>(thread_id) << 32);
  
  std::mt19937_64 gen(42 + thread_id);
  std::uniform_int_distribution<uint32_t> offset_dist(0, 0x3FFFFFFF);

  for (auto _ : state) {
    uintptr_t addr = base_addr + (offset_dist(gen) & ~0xFULL);
    hash_set.insert(addr);
    hash_set.erase(addr);
  }

  state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_Summary_AbslPerThread)
    ->Threads(15)
    ->Name("4_Absl_PerThread_15T_BrokenCrossThread");

} // namespace ddprof

BENCHMARK_MAIN();

