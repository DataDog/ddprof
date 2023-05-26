// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <alloca.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <signal.h>
#include <sstream>
#include <thread>
#include <time.h>
#include <unistd.h>

#include "ddprof_base.hpp"
#include "syscalls.hpp"

#include "CLI/CLI11.hpp"

#ifdef USE_DD_PROFILING
#  include "dd_profiling.h"
#endif

#ifdef __GLIBC__
#  include <execinfo.h>
#endif

/*****************************  SIGSEGV Handler *******************************/
static void sigsegv_handler(int sig, siginfo_t *si, void *uc) {
  // TODO this really shouldn't call printf-family functions...
  (void)uc;
#ifdef __GLIBC__
  static void *buf[4096] = {0};
  size_t sz = backtrace(buf, 4096);
#endif
  fprintf(stderr, "simplemalloc[%d]:has encountered an error and will exit\n",
          getpid());
  if (sig == SIGSEGV)
    printf("Fault address: %p\n", si->si_addr);
#ifdef __GLIBC__
  backtrace_symbols_fd(buf, sz, STDERR_FILENO);
#endif
  exit(-1);
}

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

struct Options {
  uint64_t malloc_size;
  uint64_t realloc_size;
  uint64_t loop_count;
  std::chrono::microseconds spin_duration_per_loop;
  std::chrono::microseconds sleep_duration_per_loop;
  std::chrono::milliseconds timeout_duration;
  uint32_t callstack_depth;
  uint32_t frame_size;
  uint32_t skip_free;
};

extern "C" DDPROF_NOINLINE void do_lot_of_allocations(const Options &options,
                                                      Stats &stats) {
  uint64_t nb_alloc{0};
  uint64_t alloc_bytes{0};

  auto start_time = std::chrono::steady_clock::now();
  auto deadline_time = start_time + options.timeout_duration;
  auto start_cpu = thread_cpu_clock::now();
  unsigned skip_free = 0;
  for (uint64_t i = 0; i < options.loop_count; ++i) {
    void *p = nullptr;
    if (options.malloc_size) {
      p = malloc(options.malloc_size);
      ++nb_alloc;
      alloc_bytes += options.malloc_size;
    }
    ddprof::DoNotOptimize(p);
    void *p2;
    if (options.realloc_size) {
      p2 = realloc(p, options.realloc_size);
      ++nb_alloc;
      alloc_bytes += options.realloc_size;
    } else {
      p2 = p;
    }
    ddprof::DoNotOptimize(p2);

    if (skip_free++ >= options.skip_free) {
      free(p2);
      skip_free = 0;
    }

    if (options.sleep_duration_per_loop.count()) {
      std::this_thread::sleep_for(options.sleep_duration_per_loop);
    }
    if (options.spin_duration_per_loop.count()) {
      auto target_time =
          std::chrono::steady_clock::now() + options.spin_duration_per_loop;
      do {
        volatile uint64_t sum = 1;
        for (uint64_t j = 0; j < 100; ++j) {
          sum = std::sqrt(sum) + std::sqrt(sum);
        }
      } while (std::chrono::steady_clock::now() < target_time);
    }

    if (options.timeout_duration.count() > 0 &&
        std::chrono::steady_clock::now() >= deadline_time) {
      break;
    }
  }
  auto end_cpu = thread_cpu_clock::now();
  auto end_time = std::chrono::steady_clock::now();
  stats = {nb_alloc, alloc_bytes, end_time - start_time, end_cpu - start_cpu,
           ddprof::gettid()};
}

extern "C" DDPROF_NOINLINE void recursive_call(const Options &options,
                                               Stats &stats, uint32_t depth) {
  void *stack_alloc = nullptr;
  if (options.frame_size) {
    stack_alloc = alloca(options.frame_size);
  }
  ddprof::DoNotOptimize(stack_alloc);

  if (depth == 0) {
    do_lot_of_allocations(options, stats);
  } else {
    recursive_call(options, stats, depth - 1);
  }
  DDPROF_BLOCK_TAIL_CALL_OPTIMIZATION();
}

extern "C" DDPROF_NOINLINE void wrapper(const Options &options, Stats &stats) {
  recursive_call(options, stats, options.callstack_depth);
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
  struct sigaction sigaction_handlers = {};
  sigaction_handlers.sa_sigaction = sigsegv_handler;
  sigaction_handlers.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &(sigaction_handlers), NULL);

  CLI::App app{"Simple allocation test"};

  unsigned int nb_forks{1};
  unsigned int nb_threads{1};

  Options opts;
  std::vector<std::string> exec_args;

  app.add_option("--fork", nb_forks, "Number of processes to create")
      ->default_val(1);
  app.add_option("--threads", nb_threads, "Number of threads to use")
      ->default_val(1);
  app.add_option("--exec", exec_args, "Exec the following command")
      ->expected(-1); // minus is to say at least 1
  app.add_option("--loop", opts.loop_count, "Number of loops")->default_val(0);
  app.add_option("--malloc", opts.malloc_size,
                 "Malloc allocation size per loop")
      ->default_val(1000);
  app.add_option("--realloc", opts.realloc_size,
                 "Realloc allocation size per loop")
      ->default_val(2000);
  app.add_option("--call-depth", opts.callstack_depth, "Callstack depth")
      ->default_val(0);
  app.add_option("--frame-size", opts.frame_size,
                 "Size to allocate on the stack for each frame")
      ->default_val(0);
  app.add_option("--skip-free", opts.skip_free,
                 "Only free every N allocations (default is 0)")
      ->default_val(0);

  app.add_option<std::chrono::milliseconds, int64_t>(
         "--timeout", opts.timeout_duration, "Timeout after N milliseconds")
      ->default_val(0)
      ->check(CLI::NonNegativeNumber);
  app.add_option<std::chrono::microseconds, int64_t>(
         "--sleep", opts.sleep_duration_per_loop,
         "Time to sleep (us) between allocations")
      ->default_val(0)
      ->check(CLI::NonNegativeNumber);
  app.add_option<std::chrono::microseconds, int64_t>(
         "--spin", opts.spin_duration_per_loop,
         "Time to spin (us) between allocations")
      ->default_val(0)
      ->check(CLI::NonNegativeNumber);

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
    threads.emplace_back(wrapper, std::cref(opts), std::ref(stats[i]));
  }
  wrapper(opts, stats[0]);
  for (auto &t : threads) {
    t.join();
  }
  auto pid = getpid();
  for (auto &stat : stats) {
    print_stats(pid, stat);
  }
}
