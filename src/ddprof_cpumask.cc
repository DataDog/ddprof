// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_cpumask.hpp"

#include <climits>
#include <cstdint>
#include <dirent.h>
#include <ranges>
#include <sys/sysinfo.h>
#include <sys/types.h>

namespace ddprof {

constexpr uint8_t k_a_hex_value = 0xa;

/* Parse cpu mask given as hexa string (with optional 0x prefix) */
bool parse_cpu_mask(std::string_view sv, cpu_set_t &cpu_mask) {
  /* skip 0x, it's all hex anyway */
  if (sv.starts_with("0x")) {
    sv = sv.substr(2);
  }

  CPU_ZERO(&cpu_mask);

  int cpu_idx = 0;
  for (char const c : std::ranges::reverse_view(sv)) {
    // input mask can use comma as separator
    if (c == ',') {
      continue;
    }

    uint8_t v = 0;
    if (c >= '0' && c <= '9') {
      v = c - '0';
    } else if (c >= 'A' && c <= 'F') {
      v = c - 'A' + k_a_hex_value;
    } else if (c >= 'a' && c <= 'f') {
      v = c - 'a' + k_a_hex_value;
    } else {
      return false;
    }

    for (uint32_t bitpos = 0; bitpos < 4; ++bitpos) {
      if (v & (1U << bitpos)) {
        CPU_SET(cpu_idx + bitpos, &cpu_mask);
      }
    }
    cpu_idx += 4;
  }

  return true;
}

std::string cpu_mask_to_string(const cpu_set_t &cpu_mask) {
  int const nb_cpus = sizeof(cpu_set_t) * CHAR_BIT;

  bool one_cpu_set = false;
  std::string s;
  for (int cpu_idx = nb_cpus - 4; cpu_idx >= 0; cpu_idx -= 4) {
    uint8_t v = 0;
    for (uint32_t bitpos = 0; bitpos < 4; ++bitpos) {
      if (CPU_ISSET(cpu_idx + bitpos, &cpu_mask)) {
        v |= 1 << bitpos;
      }
    }

    if (!one_cpu_set) {
      if (v) {
        s.reserve(cpu_idx / 4 + 1);
        one_cpu_set = true;
      } else {
        continue;
      }
    }

    if (v >= k_a_hex_value) {
      v = v - k_a_hex_value + 'a';
    } else {
      v = v + '0';
    }
    s += static_cast<char>(v);
  }
  return s;
}

int nprocessors_conf() {
  // Rationale: we cannot rely on get_nprocs / sysconf(_SC_NPROCESSORS_CONF)
  // because they are implemented on top of sched_getaffinity in musl libc.
  // Meaning that `taskset -c 0 getconf _NPROCESSORS_CONF` returns 1 on musl.
  int ret = 0;
  DIR *dir = opendir("/sys/devices/system/cpu");

  if (dir) {
    dirent *dp;

    while ((dp = readdir(dir))) {
      if (dp->d_type == DT_DIR && dp->d_name[0] == 'c' &&
          dp->d_name[1] == 'p' && dp->d_name[2] == 'u' &&
          isdigit(dp->d_name[3])) {
        ++ret;
      }
    }
    closedir(dir);
  } else {
    ret = get_nprocs_conf();
  }
  return ret != 0 ? ret : 1;
}

} // namespace ddprof