// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <benchmark/benchmark.h>
#include <sys/times.h>
#include <time.h>

#include "loghandle.hpp"
#include "perf_clock.hpp"
#include "tsc_clock.hpp"

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

static void BM_clock_boottime(benchmark::State &state) {
  for (auto _ : state) {
    timespec tp;
    clock_gettime(CLOCK_BOOTTIME, &tp);
  }
}

BENCHMARK(BM_clock_boottime);

static void BM_rdtsc(benchmark::State &state) {
  for (auto _ : state) {
    ddprof::TscClock::cycles_now();
  }
}

BENCHMARK(BM_rdtsc);

static void BM_clock(benchmark::State &state) {
  for (auto _ : state) {
    clock();
  }
}

BENCHMARK(BM_clock);

static void BM_clock_thread_cputime(benchmark::State &state) {
  for (auto _ : state) {
    timespec tp;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tp);
  }
}

BENCHMARK(BM_clock_thread_cputime);

static void BM_times(benchmark::State &state) {
  for (auto _ : state) {
    tms tm;
    ::times(&tm);
  }
}

BENCHMARK(BM_times);

static void BM_perf_clock(benchmark::State &state) {
  ddprof::LogHandle log_handle;
  ddprof::PerfClock::determine_perf_clock_source();
  for (auto _ : state) {
    auto t = ddprof::PerfClock::now();
    benchmark::DoNotOptimize(t);
  }
}

BENCHMARK(BM_perf_clock);
