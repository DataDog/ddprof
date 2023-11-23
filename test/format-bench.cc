#include <benchmark/benchmark.h>

#include "absl/strings/str_format.h"
#include <absl/strings/substitute.h>

#include <fmt/compile.h>
#include <fmt/core.h>
#include <fmt/format.h>
#include <format>
#include <random>

inline unsigned compute_digest(fmt::string_view data) {
  unsigned digest = 0;
  for (char c : data)
    digest += c;
  return digest;
}

struct Data {
  std::vector<int> values;
  unsigned digest;

  auto begin() const { return values.begin(); }
  auto end() const { return values.end(); }

  // Prints the number of values by digit count, e.g.
  //  1  27263
  //  2 247132
  //  3 450601
  //  4 246986
  //  5  25188
  //  6   2537
  //  7    251
  //  8     39
  //  9      2
  // 10      1
  void print_digit_counts() const {
    int counts[11] = {};
    for (auto value : values)
      ++counts[fmt::format_int(value).size()];
    fmt::print("The number of values by digit count:\n");
    for (int i = 1; i < 11; ++i)
      fmt::print("{:2} {:6}\n", i, counts[i]);
  }

  Data() : values(1'000'000) {
    // Similar data as in Boost Karma int generator test:
    // https://www.boost.org/doc/libs/1_63_0/libs/spirit/workbench/karma/int_generator.cpp
    // with rand replaced by uniform_int_distribution for consistent results
    // across platforms.
    std::mt19937 gen;
    std::uniform_int_distribution<unsigned> dist(
        0, (std::numeric_limits<int>::max)());
    std::generate(values.begin(), values.end(), [&]() {
      int scale = dist(gen) / 100 + 1;
      return static_cast<int>(dist(gen) * dist(gen)) / scale;
    });
    digest =
        std::accumulate(begin(), end(), unsigned(), [](unsigned lhs, int rhs) {
          char buffer[12];
          unsigned size = std::sprintf(buffer, "%d", rhs);
          return lhs + compute_digest({buffer, size});
        });
    print_digit_counts();
  }
} data;

void fmt_to_string(benchmark::State &state) {
  for (auto s : state) {
    for (auto value : data) {
      std::string s = fmt::to_string(value);
      benchmark::DoNotOptimize(s.data());
    }
  }
}
BENCHMARK(fmt_to_string);

void sprintf(benchmark::State &state) {
  for (auto s : state) {
    for (auto value : data) {
      char buffer[12];
      unsigned size = std::sprintf(buffer, "%d", value);
      benchmark::DoNotOptimize(buffer);
    }
  }
}
BENCHMARK(sprintf);

void abseil_strformat(benchmark::State &state) {
  for (auto s : state) {
    for (auto value : data) {
      std::string s = absl::StrFormat("%d", value);
      benchmark::DoNotOptimize(s.data());
    }
  }
}
BENCHMARK(abseil_strformat);

void abseil_substitute(benchmark::State &state) {
  for (auto s : state) {
    for (auto value : data) {
      std::string s = absl::Substitute("$0", value);
      benchmark::DoNotOptimize(s.data());
    }
  }
}
BENCHMARK(abseil_substitute);

void tinyformat_sprintf(benchmark::State &state) {
  for (auto st : state) {
    char buffer[256];
    unsigned size = std::sprintf(buffer, "%0.10f:%04d:%+g:%s:%p:%c:%%\n", 1.234,
                                 42, 3.13, "str", (void *)1000, (int)'X');
    benchmark::DoNotOptimize(buffer);
  }
}
BENCHMARK(tinyformat_sprintf);

void tinyformat_abseil_strformat(benchmark::State &state) {
  for (auto st : state) {
    std::string s = absl::StrFormat("%0.10f:%04d:%+g:%s:%p:%c:%%\n", 1.234, 42,
                                    3.13, "str", (void *)1000, (int)'X');
    benchmark::DoNotOptimize(s.data());
  }
}
BENCHMARK(tinyformat_abseil_strformat);

void tinyformat_fmt(benchmark::State &state) {
  for (auto st : state) {
    std::string s = fmt::format("{:.10f}:{:04}:{:+}:{}:{}:{}:%\n", 1.234, 42,
                                3.13, "str", (void *)1000, 'X');
    benchmark::DoNotOptimize(s.data());
  }
}
BENCHMARK(tinyformat_fmt);

void tinyformat_fmt_to(benchmark::State &state) {
  for (auto st : state) {
    char buffer[256];
    auto p = fmt::format_to(buffer, "{:.10f}:{:04}:{:+}:{}:{}:{}:%\n", 1.234,
                            42, 3.13, "str", (void *)1000, 'X');
    *p = '\0';

    benchmark::DoNotOptimize(buffer);
  }
}
BENCHMARK(tinyformat_fmt_to);

void tinyformat_fmt_compile(benchmark::State &state) {
  for (auto st : state) {
    std::string s = fmt::format(FMT_COMPILE("{:.10f}:{:04}:{:+}:{}:{}:{}:%\n"),
                                1.234, 42, 3.13, "str", (void *)1000, 'X');
    benchmark::DoNotOptimize(s.data());
  }
}
BENCHMARK(tinyformat_fmt_compile);

void tinyformat_std_format(benchmark::State &state) {
  for (auto st : state) {
    std::string s = std::format("{:.10f}:{:04}:{:+}:{}:{}:{}:%\n", 1.234, 42,
                                3.13, "str", (void *)1000, 'X');
    benchmark::DoNotOptimize(s.data());
  }
}
BENCHMARK(tinyformat_std_format);


void tinyformat_abseil_substitute(benchmark::State &state) {
  for (auto st : state) {
    std::string s = absl::Substitute("$0:$1:$2:$3:$4:$5:%\n", 1.234, 42,
                                    3.13, "str", (void *)1000, (int)'X');
    benchmark::DoNotOptimize(s.data());
  }
}
BENCHMARK(tinyformat_abseil_substitute);
