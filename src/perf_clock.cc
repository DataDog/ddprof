// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "perf_clock.hpp"

#include "chrono_utils.hpp"
#include "defer.hpp"
#include "logger.hpp"
#include "perf.hpp"
#include "perf_ringbuffer.hpp"
#include "ringbuffer_utils.hpp"
#include "tsc_clock.hpp"
#include "unique_fd.hpp"

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace ddprof {

namespace {

constexpr auto g_clock_monotonic_func = []() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return timespec_to_duration(ts);
};

constexpr auto g_clock_monotonic_raw_func = []() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return timespec_to_duration(ts);
};

constexpr auto g_tsc_clock_func = []() {
  return TscClock::now().time_since_epoch();
};

bool test_clock(PerfClockSource perf_clock_source, int current_cpu) {
  bool use_clockid = false;
  clockid_t clockid = 0;

  if (perf_clock_source < PerfClockSource::kMaxPosixClock) {
    use_clockid = true;
    clockid = static_cast<clockid_t>(perf_clock_source);
  } else if (perf_clock_source != PerfClockSource::kTSC) {
    return false;
  }

  perf_event_attr attr = {.type = PERF_TYPE_SOFTWARE,
                          .size = sizeof(struct perf_event_attr),
                          .config = PERF_COUNT_SW_DUMMY,
                          .sample_period = 1,
                          .sample_type = PERF_SAMPLE_TIME,
                          .disabled = 1,
                          .exclude_kernel = 1,
                          .exclude_hv = 1,
                          .mmap = 1,
                          .freq = 0,
                          .mmap_data = 1,
                          .sample_id_all = 1,
                          .mmap2 = 1,
                          .use_clockid = use_clockid,
                          .clockid = clockid};

  UniqueFd const fd{
      perf_event_open(&attr, 0, current_cpu, -1, PERF_FLAG_FD_CLOEXEC)};
  if (!fd) {
    LG_WRN("perf_event_open failed: %s", strerror(errno));
    return false;
  }

  auto ring_buffer_size = perf_mmap_size(1);
  void *region = perfown_sz(fd.get(), ring_buffer_size);
  if (!region) {
    LG_WRN("perfown_sz failed: %s", strerror(errno));
    return false;
  }
  defer { perfdisown(region, ring_buffer_size); };

  if (ioctl(fd.get(), PERF_EVENT_IOC_ENABLE) == -1) {
    LG_WRN("ioctl failed: %s", strerror(errno));
    return false;
  }
  RingBuffer rb;
  rb_init(&rb, region, ring_buffer_size, RingBufferType::kPerfRingBuffer);

  PerfRingBufferReader reader(&rb);

  constexpr int nb_checks = 10;
  const auto mmap_size = get_page_size();

  for (int i = 0; i < nb_checks; ++i) {
    auto t0 = PerfClock::now();
    auto *addr = mmap(nullptr, mmap_size, PROT_READ | PROT_WRITE,
                      MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    auto t1 = PerfClock::now();
    if (addr == MAP_FAILED) {
      LG_ERR("mmap failed: %s", strerror(errno));
      return false;
    }
    munmap(addr, mmap_size);
    reader.update_available();
    ConstBuffer buffer = reader.read_all_available();
    if (buffer.empty()) {
      return false;
    }
    const auto *hdr =
        reinterpret_cast<const perf_event_header *>(buffer.data());
    if (hdr->type != PERF_RECORD_MMAP2) {
      return false;
    }
    auto timestamp =
        perf_clock_time_point_from_timestamp(hdr_time(hdr, PERF_SAMPLE_TIME));
    if (t0 > timestamp || t1 < timestamp) {
      return false;
    }
    buffer = remaining(buffer, hdr->size);
    if (!buffer.empty()) {
      return false;
    }
  }

  return true;
}

} // namespace

PerfClockSource PerfClock::init() noexcept {
  auto current_cpu = sched_getcpu();
  cpu_set_t old_affinity;
  CPU_ZERO(&old_affinity);
  sched_getaffinity(0, sizeof(old_affinity), &old_affinity);
  cpu_set_t new_affinity;
  CPU_ZERO(&new_affinity);
  CPU_SET(current_cpu, &new_affinity);
  sched_setaffinity(0, sizeof(new_affinity), &new_affinity);
  defer { sched_setaffinity(0, sizeof(old_affinity), &old_affinity); };

  const auto &calibration = TscClock::calibration();
  if (calibration.state == TscClock::State::kOK &&
      calibration.method == TscClock::CalibrationMethod::kPerf) {

    PerfClock::_clock_func = g_tsc_clock_func;
    if (test_clock(PerfClockSource::kTSC, current_cpu)) {
      LG_NTC("Using TSC as perf clock source.");
      _clock_source = PerfClockSource::kTSC;
      return _clock_source;
    }
    LG_DBG("Failed to use TSC as perf clock source.");
  }

  PerfClock::_clock_func = g_clock_monotonic_func;
  if (test_clock(PerfClockSource::kClockMonotonic, current_cpu)) {
    LG_NTC("Using ClockMonotonic as perf clock source.");
    _clock_source = PerfClockSource::kClockMonotonic;
    return _clock_source;
  }

  LG_DBG("Failed to use ClockMonotonic as perf clock source.");

  PerfClock::_clock_func = g_clock_monotonic_raw_func;
  if (test_clock(PerfClockSource::kClockMonotonicRaw, current_cpu)) {
    LG_NTC("Using ClockMonotonicRaw as perf clock source.");
    _clock_source = PerfClockSource::kClockMonotonicRaw;
    return _clock_source;
  }
  LG_DBG("Failed to use ClockMonotonicRaw as perf clock source.");

  LG_WRN("Failed to find a usable clock source for perf.");
  PerfClock::_clock_func = null_clock;

  _clock_source = PerfClockSource::kNoClock;

  return _clock_source;
}

void PerfClock::init(PerfClockSource clock_source) noexcept {
  _clock_source = clock_source;
  switch (clock_source) {
  case PerfClockSource::kClockMonotonic:
    _clock_func = g_clock_monotonic_func;
    break;
  case PerfClockSource::kClockMonotonicRaw:
    _clock_func = g_clock_monotonic_raw_func;
    break;
  case PerfClockSource::kTSC:
    _clock_func = g_tsc_clock_func;
    break;
  default:
    _clock_source = PerfClockSource::kNoClock;
    _clock_func = null_clock;
  }
}

} // namespace ddprof
