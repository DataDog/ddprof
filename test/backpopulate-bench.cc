#include <benchmark/benchmark.h>

#include "dso_hdr.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace ddprof {

namespace {

void BM_dso_from_proc_line(benchmark::State &state) {
  constexpr pid_t pid = 10;
  // clang-format off
  constexpr char const* s_proc_line = "7f17dd339000-7f17dd33a000 rwxp 00383000 00:61 698929                     /usr/share/dotnet/shared/Microsoft.NETCore.App/6.0.22/libcoreclr.so";
  // clang-format on
  for (auto _ : state) {
    auto dso = DsoHdr::dso_from_proc_line(pid, s_proc_line);
    benchmark::DoNotOptimize(dso);
  }
}

void BM_backpopulate(benchmark::State &state) {
  DsoHdr dso_hdr;
  // benchmark on a specific pid
  auto *s = getenv("BENCHMARK_PID");
  auto pid = getpid();
  if (s) {
    pid = atoi(s);
  } else {
    // generate mappings for self
    constexpr int nb_mappings = 200;
    int fd = ::open("/proc/self/exe", O_RDONLY);
    struct stat st;
    fstat(fd, &st);
    for (int i = 0; i < nb_mappings; ++i) {
      mmap(nullptr, st.st_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE,
           fd, 0);
    }
  }

  for (auto _ : state) {
    int n;
    dso_hdr.pid_backpopulate(pid, n);
  }
}
} // namespace

BENCHMARK(BM_backpopulate);
BENCHMARK(BM_dso_from_proc_line);
} // namespace ddprof
