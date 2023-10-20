/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2017 Intel Corporation
 */
// Code taken from https://github.com/DPDK/dpdk

#include "tsc_clock.hpp"

#include "ddres.hpp"
#include "defer.hpp"
#include "perf.hpp"
#include "unique_fd.hpp"

#ifdef __x86_64__
#  include <cpuid.h>
#endif

#include <algorithm>
#include <array>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

using namespace std::chrono_literals;

namespace ddprof {

namespace {
// NOLINTBEGIN(readability-magic-numbers)
constexpr uint64_t k_ns_per_sec = 1'000'000'000;
constexpr uint64_t k_10mhz = 10'000'000;

#ifdef __x86_64__
unsigned int rte_cpu_get_model(uint32_t fam_mod_step) {
  uint32_t const family = (fam_mod_step >> 8) & 0xf;
  uint32_t model = (fam_mod_step >> 4) & 0xf;

  if (family == 6 || family == 15) {
    uint32_t const ext_model = (fam_mod_step >> 16) & 0xf;
    model += (ext_model << 4);
  }

  return model;
}

int32_t rdmsr(int msr, uint64_t *val) {
  int const fd = open("/dev/cpu/0/msr", O_RDONLY);
  if (fd < 0) {
    return fd;
  }

  int const ret = pread(fd, val, sizeof(uint64_t), msr);

  close(fd);
  return ret;
}

uint32_t check_model_wsm_nhm(uint8_t model) {
  switch (model) {
  /* Westmere */
  case 0x25:
  case 0x2C:
  case 0x2F:
  /* Nehalem */
  case 0x1E:
  case 0x1F:
  case 0x1A:
  case 0x2E:
    return 1;
  default:
    break;
  }

  return 0;
}

uint32_t check_model_gdm_dnv(uint8_t model) {
  switch (model) {
  /* Goldmont */
  case 0x5C:
  /* Denverton */
  case 0x5F:
    return 1;
  default:
    break;
  }

  return 0;
}

uint64_t get_tsc_freq_arch() {
  uint64_t tsc_hz = 0;
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  uint8_t mult;

  /*
   * Time Stamp Counter and Nominal Core Crystal Clock
   * Information Leaf
   */
  uint32_t const maxleaf = __get_cpuid_max(0, nullptr);

  if (maxleaf >= 0x15) {
    __cpuid(0x15, a, b, c, d);

    /* EBX : TSC/Crystal ratio, ECX : Crystal Hz */
    if (b && c) {
      return static_cast<uint64_t>(c) * (b / a);
    }
  }

  __cpuid(0x1, a, b, c, d);
  uint8_t const model = rte_cpu_get_model(a);

  if (check_model_wsm_nhm(model)) {
    mult = 133;
  } else if ((c & bit_AVX) || check_model_gdm_dnv(model)) {
    mult = 100;
  } else {
    return 0;
  }

  int32_t const ret = rdmsr(0xCE, &tsc_hz);
  if (ret < 0) {
    return 0;
  }

  return ((tsc_hz >> 8) & 0xff) * mult * 1'000'000UL;
}

#elif defined(__aarch64__)

inline uint64_t get_tsc_freq_arch() {
  uint64_t freq;

  asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
  return freq;
}

#endif

uint64_t get_tsc_freq() {
  timespec const sleeptime = {.tv_nsec = k_ns_per_sec / 50}; /* 1/50 second */

  timespec t_start;
  if (clock_gettime(CLOCK_MONOTONIC, &t_start) == 0) {
    uint64_t const start = read_tsc();
    nanosleep(&sleeptime, nullptr);
    timespec t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_end);
    uint64_t const end = read_tsc();
    uint64_t ns = ((t_end.tv_sec - t_start.tv_sec) * k_ns_per_sec);
    ns += (t_end.tv_nsec - t_start.tv_nsec);

    uint64_t const tsc_hz = ((end - start) * k_ns_per_sec) / ns;
    /* Round up to 10Mhz */
    return ((tsc_hz + k_10mhz / 2) / k_10mhz) * k_10mhz;
  }
  return 0;
}

uint64_t estimate_tsc_freq() {
  constexpr size_t max_nb_measurements = 3;
  std::array<uint64_t, max_nb_measurements> freqs;
  size_t nb_measurements = 0;
  for (size_t i = 0; i < max_nb_measurements; ++i) {
    uint64_t const freq = get_tsc_freq();
    if (freq > 0) {
      freqs[nb_measurements++] = freq;
    }
  }
  if (nb_measurements == 0) {
    return 0;
  }
  std::sort(freqs.begin(), freqs.begin() + nb_measurements);

  return freqs[(nb_measurements + 1) / 2];
}
} // namespace

bool TscClock::init_from_perf(Calibration &calibration) {
  calibration.state = State::kUnavailable;
  perf_event_attr pe = {.type = PERF_TYPE_SOFTWARE,
                        .size = sizeof(struct perf_event_attr),
                        .config = PERF_COUNT_SW_DUMMY,
                        .disabled = 1,
                        .exclude_kernel = 1,
                        .exclude_hv = 1};
  UniqueFd const fd{perf_event_open(&pe, 0, 0, -1, 0)};
  if (!fd) {
    return false;
  }

  const uint64_t sz = get_page_size();
  void *addr = mmap(nullptr, sz, PROT_READ, MAP_SHARED, fd.get(), 0);
  if (!addr) {
    return false;
  }

  defer { munmap(addr, sz); };

  auto *pc = reinterpret_cast<perf_event_mmap_page *>(addr);
  if (pc == MAP_FAILED || pc->cap_user_time != 1) {
    return false;
  }

  calibration.params.mult = pc->time_mult;
  calibration.params.shift = pc->time_shift;
  calibration.params.offset = pc->cap_user_time_zero
      ? TscClock::time_point{std::chrono::nanoseconds{pc->time_zero}}
      : TscClock::time_point{};
  calibration.method = CalibrationMethod::kPerf;
  calibration.state = State::kOK;
  return true;
}

void TscClock::init(CalibrationParams params) noexcept {
  _calibration.params = params;
  _calibration.method = CalibrationMethod::kManual;
  _calibration.state = State::kOK;
}

DDRes TscClock::init(CalibrationMethod method) noexcept {
  if ((method == CalibrationMethod::kAuto ||
       method == CalibrationMethod::kPerf) &&
      init_from_perf(_calibration)) {
    return {};
  }
  uint64_t tsc_hz = 0;

  if (method == CalibrationMethod::kAuto ||
      method == CalibrationMethod::kCpuArch) {
    _calibration.method = CalibrationMethod::kCpuArch;
    tsc_hz = get_tsc_freq_arch();
  }

  if (!tsc_hz &&
      (method == CalibrationMethod::kAuto ||
       method == CalibrationMethod::kClockMonotonicRaw)) {
    _calibration.method = CalibrationMethod::kClockMonotonicRaw;
    tsc_hz = estimate_tsc_freq();
  }

  if (!tsc_hz) {
    _calibration.state = State::kUnavailable;
    return ddres_error(DD_WHAT_TSC);
  }

  _calibration.state = State::kOK;
  auto &params = _calibration.params;
  params.offset = time_point{};
  params.shift = 31;
  params.mult = (k_ns_per_sec * (1UL << params.shift) + tsc_hz / 2) / tsc_hz;

  return {};
}

} // namespace ddprof

// NOLINTEND(readability-magic-numbers)
