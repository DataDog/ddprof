// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include "tsc_clock.hpp"

#include <chrono>
#include <thread>

namespace ddprof {

TEST(Timer, simple) {
  using namespace std::chrono_literals;

  ASSERT_TRUE(IsDDResOK(TscClock::init()));

  auto start = std::chrono::high_resolution_clock::now();
  auto c1 = TscClock::cycles_now();
  std::this_thread::sleep_for(50ms);
  auto end = std::chrono::high_resolution_clock::now();
  auto c2 = TscClock::cycles_now();
  auto d1 = end - start;
  auto d2 = TscClock::cycles_to_duration(c2 - c1);
  ASSERT_LE(std::abs((d1 - d2).count() / static_cast<double>(d1.count())),
            0.01);
}

} // namespace ddprof
