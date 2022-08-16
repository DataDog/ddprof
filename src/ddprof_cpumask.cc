// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_cpumask.hpp"

namespace ddprof {

/* Parse cpu mask given as hexa string (with optional 0x prefix) */
bool parse_cpu_mask(std::string_view sv, cpu_set_t &cpu_mask) {
  /* skip 0x, it's all hex anyway */
  if (sv.starts_with("0x")) {
    sv = sv.substr(2);
  }

  CPU_ZERO(&cpu_mask);

  int cpu_idx = 0;
  for (auto it = sv.rbegin(); it != sv.rend(); ++it) {
    const char c = *it;

    // input mask can use comma as separator
    if (c == ',') {
      continue;
    }

    uint8_t v = 0;
    if (c >= '0' && c <= '9') {
      v = c - '0';
    } else if (c >= 'A' && c <= 'F') {
      v = c - 'A' + 10;
    } else if (c >= 'a' && c <= 'f') {
      v = c - 'a' + 10;
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
  int nb_cpus = sizeof(cpu_set_t) * 8;

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

    if (v >= 10) {
      v = v - 10 + 'a';
    } else {
      v = v + '0';
    }
    s += static_cast<char>(v);
  }
  return s;
}

} // namespace ddprof