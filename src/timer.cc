/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2017 Intel Corporation
 */
// Code taken from https://github.com/DPDK/dpdk

#include "timer.hpp"

#include "ddres.hpp"
#include "defer.hpp"
#include "perf.hpp"

#ifdef __x86_64__
#  include <cpuid.h>
#endif

#include <algorithm>
#include <array>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

using namespace std::chrono_literals;

static constexpr uint64_t k_ns_per_sec = 1'000'000'000;
static constexpr uint64_t k_10mhz = 10'000'000;

#ifdef __x86_64__
static unsigned int rte_cpu_get_model(uint32_t fam_mod_step) {
  uint32_t family = (fam_mod_step >> 8) & 0xf;
  uint32_t model = (fam_mod_step >> 4) & 0xf;

  if (family == 6 || family == 15) {
    uint32_t ext_model = (fam_mod_step >> 16) & 0xf;
    model += (ext_model << 4);
  }

  return model;
}

static int32_t rdmsr(int msr, uint64_t *val) {
  int fd = open("/dev/cpu/0/msr", O_RDONLY);
  if (fd < 0)
    return fd;

  int ret = pread(fd, val, sizeof(uint64_t), msr);

  close(fd);
  return ret;
}

static uint32_t check_model_wsm_nhm(uint8_t model) {
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
  }

  return 0;
}

static uint32_t check_model_gdm_dnv(uint8_t model) {
  switch (model) {
  /* Goldmont */
  case 0x5C:
  /* Denverton */
  case 0x5F:
    return 1;
  }

  return 0;
}

static uint64_t get_tsc_freq_arch() {
  uint64_t tsc_hz = 0;
  uint32_t a, b, c, d;
  uint8_t mult;

  /*
   * Time Stamp Counter and Nominal Core Crystal Clock
   * Information Leaf
   */
  uint32_t maxleaf = __get_cpuid_max(0, NULL);

  if (maxleaf >= 0x15) {
    __cpuid(0x15, a, b, c, d);

    /* EBX : TSC/Crystal ratio, ECX : Crystal Hz */
    if (b && c)
      return static_cast<uint64_t>(c) * (b / a);
  }

  __cpuid(0x1, a, b, c, d);
  uint8_t model = rte_cpu_get_model(a);

  if (check_model_wsm_nhm(model))
    mult = 133;
  else if ((c & bit_AVX) || check_model_gdm_dnv(model))
    mult = 100;
  else
    return 0;

  int32_t ret = rdmsr(0xCE, &tsc_hz);
  if (ret < 0)
    return 0;

  return ((tsc_hz >> 8) & 0xff) * mult * 1'000'000UL;
}

#elif defined(__aarch64__)

static inline uint64_t get_tsc_freq_arch() {
  uint64_t freq;

  asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
  return freq;
}

#endif

static uint64_t get_tsc_freq() {
  timespec sleeptime = {.tv_nsec = k_ns_per_sec / 50}; /* 1/50 second */

  timespec t_start;
  if (clock_gettime(CLOCK_MONOTONIC_RAW, &t_start) == 0) {
    uint64_t start = ddprof::read_tsc();
    nanosleep(&sleeptime, NULL);
    timespec t_end;
    clock_gettime(CLOCK_MONOTONIC_RAW, &t_end);
    uint64_t end = ddprof::read_tsc();
    uint64_t ns = ((t_end.tv_sec - t_start.tv_sec) * k_ns_per_sec);
    ns += (t_end.tv_nsec - t_start.tv_nsec);

    uint64_t tsc_hz = ((end - start) * k_ns_per_sec) / ns;
    /* Round up to 10Mhz */
    return ((tsc_hz + k_10mhz / 2) / k_10mhz) * k_10mhz;
  }
  return 0;
}

static uint64_t estimate_tsc_freq() {
  constexpr size_t max_nb_measurements = 3;
  std::array<uint64_t, max_nb_measurements> freqs;
  size_t nb_measurements = 0;
  for (size_t i = 0; i < max_nb_measurements; ++i) {
    uint64_t freq = get_tsc_freq();
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

static int init_from_perf(ddprof::TscConversion &conv) {
  perf_event_attr pe = {.type = PERF_TYPE_SOFTWARE,
                        .size = sizeof(struct perf_event_attr),
                        .config = PERF_COUNT_SW_DUMMY,
                        .disabled = 1,
                        .exclude_kernel = 1,
                        .exclude_hv = 1};
  int fd = perf_event_open(&pe, 0, 0, -1, 0);
  if (fd == -1) {
    return -1;
  }
  defer { close(fd); };

  const uint64_t sz = get_page_size();
  void *addr = mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, 0);
  if (!addr) {
    return -1;
  }

  defer { munmap(addr, sz); };

  struct perf_event_mmap_page *pc =
      reinterpret_cast<perf_event_mmap_page *>(addr);
  if (pc->cap_user_time != 1) {
    return -1;
  }

  conv.mult = pc->time_mult;
  conv.shift = pc->time_shift;
  conv.state = ddprof::TscState::kOK;
  return 0;
}

namespace ddprof {

DDRes init_tsc(TscCalibrationMethod method) {
  if ((method == TscCalibrationMethod::kAuto ||
       method == TscCalibrationMethod::kPerf) &&
      !init_from_perf(g_tsc_conversion)) {
    return {};
  }
  uint64_t tsc_hz = 0;

  if (method == TscCalibrationMethod::kAuto ||
      method == TscCalibrationMethod::kCpuArch) {
    tsc_hz = get_tsc_freq_arch();
  }

  if (!tsc_hz &&
      (method == TscCalibrationMethod::kAuto ||
       method == TscCalibrationMethod::kClockMonotonicRaw)) {
    tsc_hz = estimate_tsc_freq();
  }

  if (!tsc_hz) {
    g_tsc_conversion.state = TscState::kUnavailable;
    return ddres_error(DD_WHAT_TSC);
  }

  g_tsc_conversion.state = TscState::kOK;
  g_tsc_conversion.shift = 31;
  g_tsc_conversion.mult =
      (k_ns_per_sec * (1UL << g_tsc_conversion.shift) + tsc_hz / 2) / tsc_hz;

  return {};
}

} // namespace ddprof