// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <unordered_set>

#include "address_sampler.hpp"

namespace ddprof {

TEST(address_sampler, deterministic) {
  AddressSampler sampler(AddressSampler::SamplingRate::EVERY_4);

  uintptr_t addr = 0x7f00badbeef0;

  // Same address should always return same result
  bool first = sampler.should_track(addr);
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(sampler.should_track(addr), first);
  }
}

TEST(address_sampler, cross_thread_consistent) {
  // Simulates: alloc on thread 1, free on thread 2
  // Both must agree on whether to track
  AddressSampler sampler1(AddressSampler::SamplingRate::EVERY_8);
  AddressSampler sampler2(AddressSampler::SamplingRate::EVERY_8);

  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uintptr_t> dist(0, UINTPTR_MAX);

  for (int i = 0; i < 1000; ++i) {
    uintptr_t addr = dist(gen) & ~0xFULL; // 16-byte aligned

    bool thread1_decision = sampler1.should_track(addr);
    bool thread2_decision = sampler2.should_track(addr);

    EXPECT_EQ(thread1_decision, thread2_decision)
        << "Address 0x" << std::hex << addr
        << " gave different decisions on different threads";
  }
}

TEST(address_sampler, sampling_rate) {
  constexpr int kTestCount = 100000;

  // Test EVERY_8: should track ~1/8 of addresses
  {
    AddressSampler sampler(AddressSampler::SamplingRate::EVERY_8);
    EXPECT_EQ(sampler.get_sampling_rate(), 8u);

    int tracked = 0;
    for (int i = 0; i < kTestCount; ++i) {
      uintptr_t addr = static_cast<uintptr_t>(i) * 64; // 64-byte stride
      if (sampler.should_track(addr)) {
        ++tracked;
      }
    }

    double rate = static_cast<double>(tracked) / kTestCount;
    EXPECT_GT(rate, 0.10) << "Too few tracked";
    EXPECT_LT(rate, 0.15) << "Too many tracked";
  }

  // Test EVERY_16: should track ~1/16 of addresses
  {
    AddressSampler sampler(AddressSampler::SamplingRate::EVERY_16);
    EXPECT_EQ(sampler.get_sampling_rate(), 16u);

    int tracked = 0;
    for (int i = 0; i < kTestCount; ++i) {
      uintptr_t addr = static_cast<uintptr_t>(i) * 64;
      if (sampler.should_track(addr)) {
        ++tracked;
      }
    }

    double rate = static_cast<double>(tracked) / kTestCount;
    EXPECT_GT(rate, 0.05) << "Too few tracked";
    EXPECT_LT(rate, 0.08) << "Too many tracked";
  }
}

TEST(address_sampler, sequential_addresses_distributed) {
  // Sequential addresses (like real malloc) should still be
  // well-distributed in sampling decision
  AddressSampler sampler(AddressSampler::SamplingRate::EVERY_16);

  int tracked = 0;
  constexpr int kCount = 10000;

  // Simulate sequential allocations (64-byte stride)
  uintptr_t addr = 0x7f0000000000ULL;
  for (int i = 0; i < kCount; ++i) {
    if (sampler.should_track(addr)) {
      ++tracked;
    }
    addr += 64;
  }

  double rate = static_cast<double>(tracked) / kCount;
  // Should be close to 1/16 even for sequential addresses
  EXPECT_GT(rate, 0.05);
  EXPECT_LT(rate, 0.08);
}

TEST(address_sampler, track_all) {
  AddressSampler sampler(AddressSampler::SamplingRate::EVERY_1);

  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uintptr_t> dist(0, UINTPTR_MAX);

  // With EVERY_1, all addresses should be tracked
  for (int i = 0; i < 1000; ++i) {
    uintptr_t addr = dist(gen) & ~0xFULL;
    EXPECT_TRUE(sampler.should_track(addr));
  }
}

TEST(address_sampler, realistic_malloc_pattern) {
  // Simulate realistic malloc: sequential addresses with reuse
  AddressSampler sampler(AddressSampler::SamplingRate::EVERY_32);

  std::unordered_set<uintptr_t> allocated;
  std::unordered_set<uintptr_t> tracked;

  // Simulate allocations
  uintptr_t current = 0x7f0000000000ULL;
  for (int i = 0; i < 10000; ++i) {
    uintptr_t addr = current;
    current += 64;

    allocated.insert(addr);
    if (sampler.should_track(addr)) {
      tracked.insert(addr);
    }
  }

  // Simulate frees - must check same addresses
  int correct_untrack = 0;
  for (uintptr_t addr : allocated) {
    bool was_tracked = tracked.count(addr) > 0;
    bool should_untrack = sampler.should_track(addr);

    if (was_tracked == should_untrack) {
      ++correct_untrack;
    }
  }

  // All addresses should have consistent track/untrack decision
  EXPECT_EQ(correct_untrack, static_cast<int>(allocated.size()));
}

} // namespace ddprof
