// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <alloca.h>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <functional>
#include <iostream>
#include <sstream>
#include <sys/resource.h>
#include <thread>
#include <unistd.h>

#include "clocks.hpp"
#include "ddprof_base.hpp"
#include "enum_flags.hpp"
#include "syscalls.hpp"

#ifndef SIMPLE_MALLOC_SHARED_LIBRARY
#  include "CLI/CLI11.hpp"
#endif

#ifdef USE_DD_PROFILING
#  include "dd_profiling.h"
#endif

#ifdef __GLIBC__
#  include <execinfo.h>
#endif
class Voidify final {
public:
  // This has to be an operator with a precedence lower than << but higher than
  // ?:
  template <typename T> void operator&&(const T & /*unused*/) const && {}
};

class LogMessageFatal {
public:
  LogMessageFatal(const char *file, int line, const char *failure_msg) {
    InternalStream() << "Check failed " << file << ':' << line << ':'
                     << failure_msg << " ";
  }
  ~LogMessageFatal() {
    std::cerr << '\n';
    abort();
  }
  LogMessageFatal(const LogMessageFatal &) = delete;
  LogMessageFatal &operator=(const LogMessageFatal &) = delete;

  static std::ostream &InternalStream() { return std::cerr; }
};

#define CHECK_IMPL(condition, condition_text)                                  \
  (condition) ? (void)0                                                        \
              : Voidify() &&                                                   \
          LogMessageFatal(__FILE__, __LINE__, condition_text).InternalStream()
#define CHECK(condition) CHECK_IMPL((condition), #condition)

namespace ddprof {

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
  std::chrono::milliseconds initial_delay;
  uint32_t callstack_depth;
  uint32_t frame_size;
  uint32_t skip_free;
  int nice;
  bool use_shared_library = false;
  bool avoid_dlopen_hook = false;
  bool stop{false};
};

// NOLINTBEGIN(clang-analyzer-unix.Malloc)
extern "C" DDPROF_NOINLINE void do_lot_of_allocations(const Options &options,
                                                      Stats &stats) {
  uint64_t nb_alloc{0};
  uint64_t alloc_bytes{0};

  auto start_time = std::chrono::steady_clock::now();
  auto deadline_time = start_time + options.timeout_duration;
  auto start_cpu = ThreadCpuClock::now();
  unsigned skip_free = 0;
  for (uint64_t i = 0; i < options.loop_count; ++i) {
    void *p = nullptr;
    if (options.malloc_size) {
      p = malloc(options.malloc_size);
      ++nb_alloc;
      alloc_bytes += options.malloc_size;
    }
    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
    DoNotOptimize(p);
    void *p2;
    if (options.realloc_size) {
      p2 = realloc(p, options.realloc_size);
      ++nb_alloc;
      alloc_bytes += options.realloc_size;
    } else {
      p2 = p;
    }
    // NOLINTNEXTLINE(clang-analyzer-unix.Malloc)
    DoNotOptimize(p2);

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
        constexpr size_t nb_work_iterations = 10;
        ;
        for (uint64_t j = 0; j < nb_work_iterations; ++j) {
          sum = std::sqrt(sum) + std::sqrt(sum);
        }
      } while (std::chrono::steady_clock::now() < target_time);
    }

    if (options.timeout_duration.count() > 0 &&
        std::chrono::steady_clock::now() >= deadline_time) {
      break;
    }
  }
  auto end_cpu = ThreadCpuClock::now();
  auto end_time = std::chrono::steady_clock::now();
  stats = {nb_alloc, alloc_bytes, end_time - start_time, end_cpu - start_cpu,
           ddprof::gettid()};
}

// NOLINTNEXTLINE(misc-no-recursion)
extern "C" DDPROF_NOINLINE void recursive_call(const Options &options,
                                               Stats &stats, uint32_t depth) {
  void *stack_alloc = nullptr;
  if (options.frame_size) {
    stack_alloc = alloca(options.frame_size);
  }
  DoNotOptimize(stack_alloc);

  if (depth == 0) {
    do_lot_of_allocations(options, stats);
  } else {
    recursive_call(options, stats, depth - 1);
  }
  DDPROF_BLOCK_TAIL_CALL_OPTIMIZATION();
}

extern "C" DDPROF_EXPORT DDPROF_NOINLINE void wrapper(const Options &options,
                                                      Stats &stats) {
  recursive_call(options, stats, options.callstack_depth);
}
// NOLINTEND(clang-analyzer-unix.Malloc)

using WrapperFuncPtr = decltype(&wrapper);

} // namespace ddprof

#ifndef SIMPLE_MALLOC_SHARED_LIBRARY
namespace ddprof {

/*****************************  SIGSEGV Handler *******************************/
void sigsegv_handler(int sig, siginfo_t *si, void *uc) {
  // TODO this really shouldn't call printf-family functions...
  (void)uc;
#ifdef __GLIBC__
  constexpr size_t k_stacktrace_buffer_size = 4096;
  static void *buf[k_stacktrace_buffer_size] = {};
  size_t const sz = backtrace(buf, std::size(buf));
#endif
  const char msg[] = "ddprof: encountered an error and will exit\n";
  write(STDERR_FILENO, msg, sizeof(msg) - 1);
  if (sig == SIGSEGV) {
    const char msg[] = "[DDPROF] Fault address\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
  }
#ifdef __GLIBC__
  backtrace_symbols_fd(buf, sz, STDERR_FILENO);
#endif
  _exit(-1);
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

enum class WrapperOpts : uint8_t {
  kNone = 0x0,
  kUseSharedLibrary = 0x1,
  kAvoidDLOpenHook = 0x2
};

} // namespace ddprof

ALLOW_FLAGS_FOR_ENUM(ddprof::WrapperOpts);

namespace ddprof {
std::string get_shared_library_path() {
  return std::filesystem::canonical(std::filesystem::path("/proc/self/exe"))
             .parent_path() /
      "libsimplemalloc.so";
}

WrapperFuncPtr get_wrapper_func(WrapperOpts opts) {
  static_assert(ddprof::EnableBitMaskOperators<WrapperOpts>::value);
  if ((opts & WrapperOpts::kUseSharedLibrary) == WrapperOpts::kNone) {
    return &wrapper;
  }
  auto dlopen_func = &::dlopen;
  if ((opts & WrapperOpts::kAvoidDLOpenHook) != WrapperOpts::kNone) {
    // Do not use dlopen function directly to avoid dlopen hook
    dlopen_func =
        reinterpret_cast<decltype(dlopen_func)>(dlsym(RTLD_DEFAULT, "dlopen"));
  }
  CHECK(dlopen_func) << "Unable to find dlopen: " << dlerror();

  auto library_path = get_shared_library_path();
  void *handle = dlopen_func(library_path.c_str(), RTLD_NOW);
  CHECK(handle) << "Unable to dlopen " << library_path.c_str() << ": "
                << dlerror();

  auto wrapper_func =
      reinterpret_cast<WrapperFuncPtr>(dlsym(handle, "wrapper"));
  CHECK(wrapper_func) << "Unable to find wrapper func: " << dlerror();
  return wrapper_func;
}
} // namespace ddprof

int main(int argc, char *argv[]) {
  using namespace ddprof;
  try {
    struct sigaction sigaction_handlers = {};
    sigaction_handlers.sa_sigaction = sigsegv_handler;
    sigaction_handlers.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &(sigaction_handlers), nullptr);

    CLI::App app{"Simple allocation test"};

    unsigned int nb_forks{1};
    unsigned int nb_threads{1};

    Options opts;
    std::vector<std::string> exec_args;

    constexpr size_t k_default_malloc_size{1000};
    constexpr size_t k_default_realloc_size{2000};
    app.add_option("--fork", nb_forks, "Number of processes to create")
        ->default_val(1);
    app.add_option("--threads", nb_threads, "Number of threads to use")
        ->default_val(1);
    app.add_option("--exec", exec_args, "Exec the following command")
        ->expected(-1); // minus is to say at least 1
    app.add_option("--loop", opts.loop_count, "Number of loops")
        ->default_val(0);
    app.add_option("--malloc", opts.malloc_size,
                   "Malloc allocation size per loop")
        ->default_val(k_default_malloc_size);
    app.add_option("--realloc", opts.realloc_size,
                   "Realloc allocation size per loop")
        ->default_val(k_default_realloc_size);
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
    app.add_flag("--use-shared-library", opts.use_shared_library,
                 "Make libsimplemalloc.so (with dlopen) do the allocations");
    app.add_flag("--avoid-dlopen-hook", opts.avoid_dlopen_hook,
                 "Avoid dlopen hook when loading libsimplemalloc.so");
    app.add_option("--initial-delay", opts.initial_delay, "Initial delay (ms)")
        ->default_val(0)
        ->check(CLI::NonNegativeNumber);
    app.add_option("--nice", opts.nice, "Linux niceness setting")
        ->default_val(0)
        ->check(CLI::Bound(-20, 19)); // NOLINT(readability-magic-numbers)
    app.add_flag("--stop", opts.stop,
                 "Stop process just after spawning fork / threads");

    if (opts.nice != 0) {
      setpriority(PRIO_PROCESS, 0, opts.nice);
      if (errno) {
        fprintf(stderr, "Requested nice level (%d) could not be set \n",
                opts.nice);
        return 1;
      }
    }

#  ifdef USE_DD_PROFILING
    bool start_profiling = false;
    app.add_flag("--profile", start_profiling, "Enable profiling")
        ->default_val(false);
#  endif

    CLI11_PARSE(app, argc, argv);
#  ifdef USE_DD_PROFILING
    if (start_profiling && ddprof_start_profiling() != 0) {
      fprintf(stderr, "Failed to start profiling\n");
      return 1;
    }
#  endif
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
      new_args.reserve(exec_args.size());
      for (auto &a : exec_args) {
        new_args.push_back(a.data());
      }
      new_args.push_back(nullptr);
      execvp(new_args[0], new_args.data());
      perror("Exec failed: ");
      return 1;
    }

    auto wrapper_func = get_wrapper_func(
        (opts.use_shared_library ? WrapperOpts::kUseSharedLibrary
                                 : WrapperOpts::kNone) |
        (opts.avoid_dlopen_hook ? WrapperOpts::kAvoidDLOpenHook
                                : WrapperOpts::kNone));

    if (opts.initial_delay.count() > 0) {
      std::this_thread::sleep_for(opts.initial_delay);
    }

    if (opts.avoid_dlopen_hook) {
      // Do an allocation to force a recheck of loaded libraries:
      // Check is done when a sample is sent.
      constexpr auto kBigAlloc = 1024 * 1024;
      void *p = malloc(kBigAlloc);
      ddprof::DoNotOptimize(p);
      free(p);
    }

    std::vector<std::thread> threads;
    std::vector<Stats> stats{nb_threads};
    for (unsigned int i = 1; i < nb_threads; ++i) {
      threads.emplace_back(wrapper_func, std::cref(opts), std::ref(stats[i]));
    }

    if (opts.stop) {
      raise(SIGSTOP);
    }

    wrapper_func(opts, stats[0]);
    for (auto &t : threads) {
      t.join();
    }

    auto pid = getpid();
    for (auto &stat : stats) {
      print_stats(pid, stat);
    }
  } catch (const std::exception &e) {
    fprintf(stderr, "Caught exception: %s\n", e.what());
  }
}

#endif
