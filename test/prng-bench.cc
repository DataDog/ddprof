// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <benchmark/benchmark.h>

#include "prng.hpp"

namespace ddprof {

static void BM_xoshiro256ss(benchmark::State &state) {
  xoshiro256ss rng;
  for (auto _ : state) {
    benchmark::DoNotOptimize(rng());
  }
}

BENCHMARK(BM_xoshiro256ss);

static void BM_minstd(benchmark::State &state) {
  std::minstd_rand rng{std::random_device{}()};
  for (auto _ : state) {
    benchmark::DoNotOptimize(rng());
  }
}

BENCHMARK(BM_minstd);

static void BM_mt19937(benchmark::State &state) {
  std::mt19937 rng{std::random_device{}()};
  for (auto _ : state) {
    benchmark::DoNotOptimize(rng());
  }
}

BENCHMARK(BM_mt19937);
} // namespace ddprof
