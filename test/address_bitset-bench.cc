// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <benchmark/benchmark.h>

#include <cstdlib>
#include <vector>

#include "address_bitset.hpp"

// #define ENABLE_ABSL_BENCHMARKS
#ifdef ENABLE_ABSL_BENCHMARKS
#  include "absl/container/flat_hash_set.h"
#endif


// Benchmarks support two contention modes:
// - kHighContention: All threads share same addresses (worst-case, static pool)
// - kLowContention: Each thread has unique addresses (realistic, thread_local pool)

namespace ddprof {

namespace {

constexpr size_t kSmallAddressPool = 1000;
constexpr size_t kMediumAddressPool = 5000;
constexpr size_t kLargeAddressPool = 10000;
constexpr size_t kVeryLargeAddressPool = 50000;
constexpr size_t kMaxLiveAddresses = 4000;
constexpr size_t kRemoveBatchDivisor = 10;
constexpr size_t kRemoveLookback = 1000;
constexpr size_t kDefaultAllocSize = 1024;
constexpr size_t kSmallTableSize = 65536;

#ifdef ENABLE_ABSL_BENCHMARKS
constexpr size_t kMaxTracked = 524288;
constexpr size_t kMaxTrackedTest = 16384;
#endif

enum class ContentionMode : std::uint8_t {
  kHighContention,   // All threads share same addresses (worst-case)
  kLowContention     // Each thread has unique addresses (realistic)
};

std::vector<uintptr_t> capture_real_malloc_addresses(size_t count,
                                                     size_t alloc_size) {
  std::vector<void *> allocations;
  std::vector<uintptr_t> addresses;
  allocations.reserve(count);
  addresses.reserve(count);

  for (size_t i = 0; i < count; ++i) {
    void *ptr = malloc(alloc_size);
    if (ptr) {
      allocations.push_back(ptr);
      addresses.push_back(reinterpret_cast<uintptr_t>(ptr));
    }
  }

  for (void *ptr : allocations) {
    free(ptr);
  }

  return addresses;
}

template <ContentionMode Mode>
const std::vector<uintptr_t> &get_address_pool(size_t pool_size, size_t alloc_size) {
  if constexpr (Mode == ContentionMode::kHighContention) {
    static std::vector<uintptr_t> addresses = 
        capture_real_malloc_addresses(pool_size, alloc_size);
    return addresses;
  } else {
    thread_local std::vector<uintptr_t> addresses = 
        capture_real_malloc_addresses(pool_size, alloc_size);
    return addresses;
  }
}

void BM_AddressBitset_RealAddresses(benchmark::State &state) {
  AddressBitset bitset(AddressBitset::_k_default_table_size);

  static std::vector<uintptr_t> addresses =
      capture_real_malloc_addresses(kLargeAddressPool, kDefaultAllocSize);

  size_t idx = 0;
  for (auto _ : state) {
    uintptr_t addr = addresses[idx % addresses.size()];
    bitset.add(addr);
    benchmark::DoNotOptimize(bitset.remove(addr));
    ++idx;
  }

  state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_AddressBitset_RealAddresses);

template <ContentionMode Mode>
void BM_AddressBitset_RealAddresses_MT(benchmark::State &state) {
  static AddressBitset bitset(AddressBitset::_k_default_table_size);

  const auto &addresses = get_address_pool<Mode>(kLargeAddressPool, kDefaultAllocSize);

  size_t idx = 0;
  for (auto _ : state) {
    uintptr_t addr = addresses[idx % addresses.size()];
    bitset.add(addr);
    benchmark::DoNotOptimize(bitset.remove(addr));
    ++idx;
  }

  state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_AddressBitset_RealAddresses_MT<ContentionMode::kHighContention>)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8);
BENCHMARK(BM_AddressBitset_RealAddresses_MT<ContentionMode::kLowContention>)
    ->Threads(1)
    ->Threads(4)
    ->Threads(8);

void BM_AddressBitset_HighLoadFactor(benchmark::State &state) {
  AddressBitset bitset(kSmallTableSize);

  static std::vector<uintptr_t> addresses =
      capture_real_malloc_addresses(kVeryLargeAddressPool, kDefaultAllocSize);

  for (size_t i = 0; i < addresses.size() / 2; ++i) {
    bitset.add(addresses[i]);
  }

  size_t idx = addresses.size() / 2;
  for (auto _ : state) {
    uintptr_t addr = addresses[idx % addresses.size()];

    if (!bitset.add(addr)) {
      bitset.remove(addresses[(idx - kRemoveLookback) % addresses.size()]);
      bitset.add(addr);
    }

    ++idx;
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddressBitset_HighLoadFactor);

#ifdef ENABLE_ABSL_BENCHMARKS
void BM_Absl_HighLoadFactor(benchmark::State &state) {
  absl::flat_hash_set<uintptr_t> set(kMaxTracked);

  static std::vector<uintptr_t> addresses =
      capture_real_malloc_addresses(kVeryLargeAddressPool, kDefaultAllocSize);

  for (size_t i = 0; i < addresses.size() / 2; ++i) {
    set.insert(addresses[i]);
  }

  size_t idx = addresses.size() / 2;
  for (auto _ : state) {
    uintptr_t addr = addresses[idx % addresses.size()];

    if (!set.insert(addr).second) {
      set.erase(addresses[(idx - kRemoveLookback) % addresses.size()]);
      set.insert(addr);
    }

    ++idx;
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Absl_HighLoadFactor);
#endif // ENABLE_ABSL_BENCHMARKS

#ifdef ENABLE_ABSL_BENCHMARKS
void BM_Absl_RealAddresses(benchmark::State &state) {
  absl::flat_hash_set<uintptr_t> set(kMaxTrackedTest);

  static std::vector<uintptr_t> addresses =
      capture_real_malloc_addresses(kLargeAddressPool, kDefaultAllocSize);

  size_t idx = 0;
  for (auto _ : state) {
    uintptr_t addr = addresses[idx % addresses.size()];
    set.insert(addr);
    benchmark::DoNotOptimize(set.erase(addr));
    ++idx;
  }

  state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_Absl_RealAddresses);
#endif // ENABLE_ABSL_BENCHMARKS

#ifdef ENABLE_ABSL_BENCHMARKS
template <ContentionMode Mode>
void BM_Absl_RealAddresses_MT(benchmark::State &state) {
  static absl::flat_hash_set<uintptr_t> set(kMaxTrackedTest);

  if (state.thread_index() == 0) {
    set.clear();
  }

  const auto &addresses = get_address_pool<Mode>(kLargeAddressPool, kDefaultAllocSize);

  size_t idx = 0;
  for (auto _ : state) {
    uintptr_t addr = addresses[idx % addresses.size()];
    set.insert(addr);
    benchmark::DoNotOptimize(set.erase(addr));
    ++idx;
  }

  state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_Absl_RealAddresses_MT<ContentionMode::kHighContention>)->Threads(1)->Threads(4)->Threads(8);
BENCHMARK(BM_Absl_RealAddresses_MT<ContentionMode::kLowContention>)->Threads(1)->Threads(4)->Threads(8);
#endif // ENABLE_ABSL_BENCHMARKS

template <ContentionMode Mode>
void BM_AddressBitset_LiveTracking(benchmark::State &state) {
  static AddressBitset bitset(AddressBitset::_k_default_table_size);

  const auto &addresses = get_address_pool<Mode>(kVeryLargeAddressPool, kDefaultAllocSize);

  thread_local std::vector<uintptr_t> live_addresses;
  live_addresses.reserve(kMediumAddressPool);

  size_t idx = 0;
  for (auto _ : state) {
    uintptr_t new_addr = addresses[idx % addresses.size()];
    if (bitset.add(new_addr)) {
      live_addresses.push_back(new_addr);
    }

    if (live_addresses.size() > kMaxLiveAddresses) {
      size_t to_remove = live_addresses.size() / kRemoveBatchDivisor;
      for (size_t i = 0; i < to_remove; ++i) {
        bitset.remove(live_addresses[i]);
      }
      live_addresses.erase(live_addresses.begin(),
                           live_addresses.begin() + to_remove);
    }

    ++idx;
    benchmark::DoNotOptimize(live_addresses.size());
  }

  for (auto addr : live_addresses) {
    bitset.remove(addr);
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddressBitset_LiveTracking<ContentionMode::kHighContention>)->Threads(1)->Threads(4)->Threads(8);
BENCHMARK(BM_AddressBitset_LiveTracking<ContentionMode::kLowContention>)->Threads(1)->Threads(4)->Threads(8);

#ifdef ENABLE_ABSL_BENCHMARKS
template <ContentionMode Mode>
void BM_Absl_LiveTracking(benchmark::State &state) {
  static absl::flat_hash_set<uintptr_t> set(kMaxTracked);

  if (state.thread_index() == 0) {
    set.clear();
  }

  const auto &addresses = get_address_pool<Mode>(kVeryLargeAddressPool, kDefaultAllocSize);

  thread_local std::vector<uintptr_t> live_addresses;
  live_addresses.reserve(kMediumAddressPool);

  size_t idx = 0;
  for (auto _ : state) {
    uintptr_t new_addr = addresses[idx % addresses.size()];
    if (set.insert(new_addr).second) {
      live_addresses.push_back(new_addr);
    }

    if (live_addresses.size() > kMaxLiveAddresses) {
      size_t to_remove = live_addresses.size() / kRemoveBatchDivisor;
      for (size_t i = 0; i < to_remove; ++i) {
        set.erase(live_addresses[i]);
      }
      live_addresses.erase(live_addresses.begin(),
                           live_addresses.begin() + to_remove);
    }

    ++idx;
    benchmark::DoNotOptimize(live_addresses.size());
  }

  for (auto addr : live_addresses) {
    set.erase(addr);
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Absl_LiveTracking<ContentionMode::kHighContention>)->Threads(1)->Threads(4)->Threads(8);
BENCHMARK(BM_Absl_LiveTracking<ContentionMode::kLowContention>)->Threads(1)->Threads(4)->Threads(8);
#endif // ENABLE_ABSL_BENCHMARKS

template <ContentionMode Mode>
void BM_AddressBitset_FreeLookupMiss(benchmark::State &state) {
  static AddressBitset bitset(AddressBitset::_k_default_table_size);

  const auto &tracked = get_address_pool<Mode>(kSmallAddressPool, kDefaultAllocSize);
  const auto &untracked = get_address_pool<Mode>(kLargeAddressPool, kDefaultAllocSize);

  if (state.thread_index() == 0 && state.iterations() == 0) {
    for (auto addr : tracked) {
      bitset.add(addr);
    }
  }

  size_t idx = 0;
  for (auto _ : state) {
    uintptr_t addr = untracked[idx % untracked.size()];
    benchmark::DoNotOptimize(bitset.remove(addr));
    ++idx;
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddressBitset_FreeLookupMiss<ContentionMode::kHighContention>)->Threads(1)->Threads(4)->Threads(8);
BENCHMARK(BM_AddressBitset_FreeLookupMiss<ContentionMode::kLowContention>)->Threads(1)->Threads(4)->Threads(8);

#ifdef ENABLE_ABSL_BENCHMARKS
template <ContentionMode Mode>
void BM_Absl_FreeLookupMiss(benchmark::State &state) {
  static absl::flat_hash_set<uintptr_t> set(kMaxTrackedTest);

  const auto &tracked = get_address_pool<Mode>(kSmallAddressPool, kDefaultAllocSize);
  const auto &untracked = get_address_pool<Mode>(kLargeAddressPool, kDefaultAllocSize);

  if (state.thread_index() == 0) {
    set.clear();
    for (auto addr : tracked) {
      set.insert(addr);
    }
  }

  size_t idx = 0;
  for (auto _ : state) {
    uintptr_t addr = untracked[idx % untracked.size()];
    benchmark::DoNotOptimize(set.erase(addr));
    ++idx;
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Absl_FreeLookupMiss<ContentionMode::kHighContention>)->Threads(1)->Threads(4)->Threads(8);
BENCHMARK(BM_Absl_FreeLookupMiss<ContentionMode::kLowContention>)->Threads(1)->Threads(4)->Threads(8);
#endif // ENABLE_ABSL_BENCHMARKS

} // namespace

} // namespace ddprof

BENCHMARK_MAIN();
