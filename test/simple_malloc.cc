// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <thread>
#include <time.h>
#include <unistd.h>

#include "ddprof_base.hpp"
#include "syscalls.hpp"

#include "CLI/CLI11.hpp"

#ifdef USE_DD_PROFILING
#  include "dd_profiling.h"
#endif

struct thread_cpu_clock {
  using duration = std::chrono::nanoseconds;
  using rep = duration::rep;
  using period = duration::period;
  using time_point = std::chrono::time_point<thread_cpu_clock, duration>;

  static constexpr bool is_steady = true;

  static time_point now() noexcept {
    timespec tp;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tp);
    return time_point(duration(std::chrono::seconds{tp.tv_sec} +
                               std::chrono::nanoseconds{tp.tv_nsec}));
  }
};

struct Stats {
  uint64_t nb_allocations;
  uint64_t allocated_bytes;
  std::chrono::nanoseconds wall_time;
  std::chrono::nanoseconds cpu_time;
  pid_t tid;
};

extern "C" DDPROF_NOINLINE void
do_lot_of_allocations(uint64_t loop_count, std::chrono::microseconds sleep,
                      std::chrono::microseconds spin,
                      std::chrono::milliseconds timeout, Stats *stats) {
  uint64_t nb_alloc{0};
  uint64_t alloc_bytes{0};

  auto start_time = std::chrono::steady_clock::now();
  auto end_time = start_time + timeout;
  auto start_cpu = thread_cpu_clock::now();
  for (uint64_t i = 0; i < loop_count; ++i) {
    void *p = malloc(1000);
    ++nb_alloc;
    alloc_bytes += 1000;
    void *p2 = realloc(p, 2000);
    ++nb_alloc;
    alloc_bytes += 2000;
    free(p2);
    void *p3 = calloc(1, 512);
    ++nb_alloc;
    alloc_bytes += 512;
    free(p3);
    if (sleep.count()) {
      std::this_thread::sleep_for(sleep);
    }
    if (spin.count()) {
      auto target_time = std::chrono::steady_clock::now() + spin;
      do {
        volatile uint64_t sum = 1;
        for (uint64_t j = 0; j < 100; ++j) {
          sum = std::sqrt(sum) + std::sqrt(sum);
        }
      } while (std::chrono::steady_clock::now() < target_time);
    }

    if (timeout.count() > 0 && std::chrono::steady_clock::now() >= end_time) {
      break;
    }
  }
  auto end_cpu = thread_cpu_clock::now();
  end_time = std::chrono::steady_clock::now();
  *stats = {nb_alloc, alloc_bytes, end_time - start_time, end_cpu - start_cpu,
            ddprof::gettid()};
}

void print_header() {
  printf("TestHeaders:%-8s,%-8s,%-14s,%-14s,%-14s,%-14s\n", "PID", "TID",
         "alloc_samples", "alloc_bytes", "wall_time", "cpu_time");
}

void print_stats(pid_t pid, const Stats &stats) {
  printf("TestStats  :%-8d,%-8d,%-14lu,%-14lu,%-14lu,%-14lu\n", pid, stats.tid,
         stats.nb_allocations, stats.allocated_bytes, stats.wall_time.count(),
         stats.cpu_time.count());
}

int main(int argc, char *argv[]) {
  CLI::App app{"Simple allocation test"};

  unsigned int nb_forks{1};
  unsigned int nb_threads{1};
  unsigned int sleep_us{0};
  unsigned int spin_us{0};
  int timeout_ms = -1;
  uint64_t loop_count = std::numeric_limits<uint64_t>::max();
  std::vector<std::string> exec_args;

  app.add_option("--fork", nb_forks, "Number of processes to create")
      ->default_val(1);
  app.add_option("--threads", nb_threads, "Number of threads to use")
      ->default_val(1);
  app.add_option("--exec", exec_args, "Exec the following command")
      ->expected(-1);
  app.add_option("--loop", loop_count, "Number of loops")->default_val(-1);
  app.add_option("--timeout", timeout_ms, "Timeout after N milliseconds")
      ->default_val(0);
  app.add_option("--sleep", sleep_us, "Time to sleep (us) between allocations")
      ->default_val(0);
  app.add_option("--spin", spin_us, "Time to spin (us) between allocations")
      ->default_val(0);
#ifdef USE_DD_PROFILING
  bool start_profiling = false;
  app.add_flag("--profile", start_profiling, "Enable profiling")
      ->default_val(false);
#endif

  CLI11_PARSE(app, argc, argv);

#ifdef USE_DD_PROFILING
  if (start_profiling && ddprof_start_profiling() != 0) {
    fprintf(stderr, "Failed to start profiling\n");
    return 1;
  }
#endif

  if (exec_args.empty()) {
    print_header();
  }

  for (unsigned int i = 1; i < nb_forks; ++i) {
    if (fork()) {
      break;
    }
  }

  if (!exec_args.empty()) {
    std::vector<char *> new_args;
    for (auto &a : exec_args) {
      new_args.push_back(a.data());
    }
    execvp(new_args[0], new_args.data());
    perror("Exec failed: ");
    return 1;
  }

  std::vector<std::thread> threads;
  std::vector<Stats> stats{nb_threads};
  for (unsigned int i = 1; i < nb_threads; ++i) {
    threads.emplace_back(do_lot_of_allocations, loop_count,
                         std::chrono::microseconds{sleep_us},
                         std::chrono::microseconds{spin_us},
                         std::chrono::milliseconds{timeout_ms}, &stats[i]);
  }
  do_lot_of_allocations(loop_count, std::chrono::microseconds{sleep_us},
                        std::chrono::microseconds{spin_us},
                        std::chrono::milliseconds{timeout_ms}, &stats[0]);
  for (auto &t : threads) {
    t.join();
  }
  auto pid = getpid();
  for (auto &stat : stats) {
    print_stats(pid, stat);
  }
}
