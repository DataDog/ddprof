// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <benchmark/benchmark.h>

#include <random>
#include <vector>

#include "address_sampler.hpp"

namespace ddprof {

// Benchmark: Single-threaded sampling decision
static void BM_AddressSampler_SingleThreaded(benchmark::State &state) {
  AddressSampler sampler(AddressSampler::SamplingRate::EVERY_16);

  std::mt19937_64 gen(42);
  std::uniform_int_distribution<uintptr_t> dist(0, UINTPTR_MAX);

  std::vector<uintptr_t> addresses;
  addresses.reserve(10000);
  for (int i = 0; i < 10000; ++i) {
    addresses.push_back(dist(gen) & ~0xFULL);
  }

  size_t idx = 0;
  for (auto _ : state) {
    uintptr_t addr = addresses[idx % addresses.size()];
    benchmark::DoNotOptimize(sampler.should_track(addr));
    ++idx;
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddressSampler_SingleThreaded);

// Benchmark: Multi-threaded (should scale linearly - no contention!)
static void BM_AddressSampler_MultiThreaded(benchmark::State &state) {
  AddressSampler sampler(AddressSampler::SamplingRate::EVERY_16);

  const int thread_id = state.thread_index();
  const uintptr_t base_addr = 0x7f0000000000ULL +
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
    benchmark::DoNotOptimize(sampler.should_track(addr));
    ++idx;
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddressSampler_MultiThreaded)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8)
    ->Threads(15)
    ->Threads(20);

// Benchmark: Sequential addresses (realistic malloc pattern)
static void BM_AddressSampler_Sequential(benchmark::State &state) {
  AddressSampler sampler(AddressSampler::SamplingRate::EVERY_32);

  const int thread_id = state.thread_index();
  const uintptr_t base = 0x7f0000000000ULL +
                          (static_cast<uintptr_t>(thread_id) << 28);

  uintptr_t addr = base;
  constexpr uintptr_t kAllocSize = 64;

  for (auto _ : state) {
    benchmark::DoNotOptimize(sampler.should_track(addr));
    addr += kAllocSize;

    if ((addr - base) > 0x10000000) {
      addr = base;
    }
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddressSampler_Sequential)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8)
    ->Threads(15);

// Benchmark: Compare sampling decision speed to actual tracking
// This shows the overhead difference between stateless sampling
// and maintaining a hash table
static void BM_Comparison_Sampling(benchmark::State &state) {
  AddressSampler sampler(AddressSampler::SamplingRate::EVERY_1);

  const int thread_id = state.thread_index();
  std::mt19937_64 gen(42 + thread_id);
  std::uniform_int_distribution<uintptr_t> dist(0, UINTPTR_MAX);

  for (auto _ : state) {
    uintptr_t addr = dist(gen) & ~0xFULL;
    benchmark::DoNotOptimize(sampler.should_track(addr));
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Comparison_Sampling)
    ->Threads(15)
    ->Name("Stateless_Sampling_15threads");

} // namespace ddprof

BENCHMARK_MAIN();

