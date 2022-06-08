// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <benchmark/benchmark.h>
#include <time.h>

#include "timer.hpp"

static void BM_clock_monotonic_raw(benchmark::State &state) {
  // Code inside this loop is measured repeatedly
  for (auto _ : state) {
    timespec tp;
    clock_gettime(CLOCK_MONOTONIC_RAW, &tp);
  }
}

BENCHMARK(BM_clock_monotonic_raw);

static void BM_clock_monotonic(benchmark::State &state) {
  for (auto _ : state) {
    timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
  }
}

BENCHMARK(BM_clock_monotonic);

static void BM_rdtsc(benchmark::State &state) {
  for (auto _ : state) {
    ddprof::get_tsc_cycles();
  }
}

BENCHMARK(BM_rdtsc);
