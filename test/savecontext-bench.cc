// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <benchmark/benchmark.h>

#include "ddprof_base.hpp"
#include "perf.hpp"
#include "perf_archmap.hpp"
#include "savecontext.hpp"
#include "syscalls.hpp"

#include <thread>

DDPROF_NOINLINE static void *get_stack_start() {
  void *stack_addr;
  size_t stack_size;
  pthread_attr_t attrs;
  pthread_getattr_np(pthread_self(), &attrs);
  pthread_attr_getstack(&attrs, &stack_addr, &stack_size);
  return stack_addr;
}

DDPROF_NOINLINE static void *get_stack_start_tls() {
  thread_local static void *stack_addr = nullptr;
  if (!stack_addr) {
    size_t stack_size;
    pthread_attr_t attrs;
    pthread_getattr_np(pthread_self(), &attrs);
    pthread_attr_getstack(&attrs, &stack_addr, &stack_size);
  }
  return stack_addr;
}

static void BM_SaveContext(benchmark::State &state) {
  uint64_t regs[PERF_REGS_COUNT];
  std::byte stack[k_default_perf_sample_stack_user];
  ddprof::span<const std::byte> stack_bounds = retrieve_stack_bounds();
  if (stack_bounds.empty()) {
    exit(1);
  }
  for (auto _ : state) {
    save_context(stack_bounds, regs, stack);
  }
}

BENCHMARK(BM_SaveContext);

static void BM_GetStackStart(benchmark::State &state) {
  for (auto _ : state) {
    get_stack_start();
  }
}

BENCHMARK(BM_GetStackStart);

static void BM_GetStackStartInThread(benchmark::State &state) {
  std::thread t([&] {
    for (auto _ : state) {
      get_stack_start();
    }
  });
  t.join();
}

BENCHMARK(BM_GetStackStartInThread);

static void BM_GetStackStartTLS(benchmark::State &state) {
  for (auto _ : state) {
    get_stack_start_tls();
  }
}

BENCHMARK(BM_GetStackStartTLS);

static void BM_GetStackStartInThreadTLS(benchmark::State &state) {
  std::thread t([&] {
    for (auto _ : state) {
      get_stack_start_tls();
    }
  });
  t.join();
}

BENCHMARK(BM_GetStackStartInThreadTLS);

static void BM_GetPID(benchmark::State &state) {
  for (auto _ : state) {
    getpid();
  }
}

BENCHMARK(BM_GetPID);

static void BM_GetTID(benchmark::State &state) {
  for (auto _ : state) {
    ddprof::gettid();
  }
}

BENCHMARK(BM_GetTID);
