// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "perf.hpp"

#include "defer.hpp"
#include "logger.hpp"

#include <cassert>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace ddprof {

namespace {
long s_page_size = 0;
constexpr size_t k_default_page_size{4096}; // Concerned about hugepages?

void set_perf_clock_source(perf_event_attr &attr,
                           PerfClockSource perf_clock_source) {
  attr.use_clockid = 0;
  if (perf_clock_source < PerfClockSource::kMaxPosixClock) {
    attr.use_clockid = 1;
    attr.clockid = static_cast<clockid_t>(perf_clock_source);
  }
}
} // namespace

namespace {
struct perf_event_attr g_dd_native_attr = {
    .size = sizeof(struct perf_event_attr),
    .disabled = 1,
    .inherit = 1,
    .exclude_kernel = 1,
    .exclude_hv = 1,
    .mmap = 0, // keep track of executable mappings
    .comm = 0, // Follow exec()
    .inherit_stat = 0,
    .enable_on_exec = 1,
    .task = 0, // Follow fork/stop events
    .precise_ip = 2,
    .mmap_data = 0, // keep track of other mappings
    .sample_id_all = 1,
    .sample_regs_user = k_perf_register_mask,
};
} // namespace

long get_page_size() {
  if (!s_page_size) {
    s_page_size = sysconf(_SC_PAGESIZE);
    // log if we have an unusual page size
    if (s_page_size != k_default_page_size) {
      LG_WRN("Page size is %ld", s_page_size);
    }
  }
  return s_page_size;
}

int perf_event_open(struct perf_event_attr *attr, pid_t pid, int cpu, int gfd,
                    unsigned long flags) {
  return syscall(__NR_perf_event_open, attr, pid, cpu, gfd, flags);
}

const char *perf_type_str(int type_id) {
  switch (type_id) {
  case PERF_TYPE_HARDWARE:
    return "HARDWARE";
  case PERF_TYPE_SOFTWARE:
    return "SOFTWARE";
  case PERF_TYPE_TRACEPOINT:
    return "TRACEPOINT";
  case PERF_TYPE_HW_CACHE:
    return "HW_CACHE";
  case PERF_TYPE_RAW:
    return "RAW";
  case PERF_TYPE_BREAKPOINT:
    return "BREAKPOINT";
  default:
    return "UNKNOWN";
  }
  return "UNKNOWN";
}

perf_event_attr perf_config_from_watcher(const PerfWatcher *watcher,
                                         bool extras,
                                         PerfClockSource perf_clock_source) {
  struct perf_event_attr attr = g_dd_native_attr;
  attr.type = watcher->type;
  attr.config = watcher->config;
  attr.sample_period = watcher->sample_period; // Equivalently, freq
  attr.freq = watcher->options.is_freq;
  attr.sample_type = watcher->sample_type;
  attr.sample_stack_user = watcher->options.stack_sample_size;

  // If use_kernel==off means we exclude_kernel
  attr.exclude_kernel =
      (watcher->options.use_kernel == PerfWatcherUseKernel::kOff);

  // Extras (metadata for tracking process state)
  if (extras) {
    attr.mmap = 1;
    attr.mmap2 = 1;
    attr.task = 1;
    attr.comm = 1;
    attr.use_clockid = 1;
    attr.clockid = CLOCK_MONOTONIC;
  }

  set_perf_clock_source(attr, perf_clock_source);

  return attr;
}

size_t perf_mmap_size(int buf_size_shift) {
  // size of buffers are constrained to a power of 2 + 1
  return ((1U << buf_size_shift) + 1) * get_page_size();
}

size_t get_mask_from_size(size_t size) {
  // assumption is that we used a (power of 2) + 1 (refer to perf_mmap_size)
  return (size - get_page_size() - 1);
}

void *perfown_sz(int fd, size_t size_of_buffer) {
  // Map in the region representing the ring buffer, map the buffer twice
  // (minus metadata size) to avoid handling boundaries.
  size_t const total_length = (2 * size_of_buffer) - get_page_size();
  // Reserve twice the size of the buffer
  void *region = mmap(nullptr, total_length, PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (MAP_FAILED == region || !region) {
    return nullptr;
  }

  auto defer_munmap = make_defer([&]() { perfdisown(region, size_of_buffer); });

  auto *ptr = static_cast<std::byte *>(region);

  // Each mapping of fd must have a size of 2^n+1 pages
  // That's why starts by mapping buffer on the second half of reserved
  // space and ensure that metadata page overlaps on the first part
  if (mmap(ptr + size_of_buffer - get_page_size(), size_of_buffer,
           PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd,
           0) == MAP_FAILED) {
    return nullptr;
  }

  // Map buffer a second time on the first half of reserved space
  // It will overlap the metadata page of the previous mapping.
  if (mmap(ptr, size_of_buffer, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED,
           fd, 0) == MAP_FAILED) {
    return nullptr;
  }

  defer_munmap.release();

  if (fcntl(fd, F_SETFL, O_RDWR | O_NONBLOCK) == -1) {
    LG_WRN("Unable to run fcntl on %d", fd);
  }

  return region;
}

int perfdisown(void *region, size_t size) {
  auto *ptr = static_cast<std::byte *>(region);

  return (munmap(ptr + size - get_page_size(), size) == 0) &&
          (munmap(ptr, size) == 0) &&
          (munmap(ptr, (2 * size) - get_page_size()) == 0)
      ? 0
      : -1;
}

// return attr sorted by priority
std::vector<perf_event_attr>
all_perf_configs_from_watcher(const PerfWatcher *watcher, bool extras,
                              PerfClockSource perf_clock_source) {
  std::vector<perf_event_attr> ret_attr;
  auto attr = perf_config_from_watcher(watcher, extras, perf_clock_source);

  ret_attr.push_back(attr);
  if (watcher->options.use_kernel == PerfWatcherUseKernel::kTry) {
    // duplicate the config, while excluding kernel
    attr.exclude_kernel = true;
    ret_attr.push_back(attr);
  }
  return ret_attr;
}

uint64_t perf_value_from_sample(const PerfWatcher *watcher,
                                const perf_event_sample *sample) {
  uint64_t val = 0;
  if (watcher->value_source == EventConfValueSource::kRaw) {
    if (PERF_SAMPLE_RAW & watcher->sample_type) {
      uint64_t const raw_offset = watcher->raw_off;
      uint64_t const raw_sz = watcher->raw_sz;
      if (raw_sz + raw_offset <= sample->size_raw) {
        assert(0 && "Overflow in raw event access");
        LG_WRN("Overflow in raw event access");
        return 0;
      }
      switch (raw_sz) {
      case 1:
        val = *reinterpret_cast<const uint8_t *>(sample->data_raw + raw_offset);
        break;
      case 2:
        val =
            *reinterpret_cast<const uint16_t *>(sample->data_raw + raw_offset);
        break;
      case 4:
        val =
            *reinterpret_cast<const uint32_t *>(sample->data_raw + raw_offset);
        break;
      case 8: // NOLINT(readability-magic-numbers)
        val =
            *reinterpret_cast<const uint64_t *>(sample->data_raw + raw_offset);
        break;
      default:
        assert(0 && "Non-integral size for raw value");
        LG_WRN("Non-integral size for raw value");
        val = 0;
        break;
      }
      return val;
    } // unexpected config
    assert(0 && "Inconsistent raw config between watcher and perf event");
    LG_WRN("Unexpected watcher configuration -- No Raw events");
    return 0;
  }
  // Register value
  if (watcher->value_source == EventConfValueSource::kRegister) {
    return sample->regs[watcher->regno];
  }

  // period by default
  assert(watcher->value_source == EventConfValueSource::kSample &&
         "All watcher types were considered");
  return sample->period;
}

} // namespace ddprof
