// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "pevent_lib.hpp"

#include "ddprof_cmdline.hpp"
#include "ddres.hpp"
#include "defer.hpp"
#include "lib/allocation_event.hpp"
#include "perf.hpp"
#include "ringbuffer_utils.hpp"
#include "sys_utils.hpp"
#include "syscalls.hpp"
#include "tracepoint_config.hpp"
#include "user_override.hpp"

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

// #define EBPF_UNWINDING
#ifdef EBPF_UNWINDING
extern "C" {
#  include "bpf/sample_processor.h"
#  include "sample_processor.skel.h"
#  include <bpf/bpf.h>
#  include <bpf/libbpf.h>
}

// todo move me in relevant place
/* Receive events from the ring buffer. */
static int event_handler(void *_ctx, void *data, size_t size) {
  stacktrace_event *event = reinterpret_cast<stacktrace_event *>(data);
  fprintf(stderr, "Event[%d] -- COMM:%s, CPU=%d\n",
          event->pid,
          event->comm,
          event->cpu_id);

  if (event->kstack_sz <= 0 && event->ustack_sz <= 0) {
    fprintf(stderr, "error in bpf handler %d", __LINE__);
    return 1;
  }
  fprintf(stderr, "\n");
  if (_ctx) {
    ddprof::BPFEvents *bpf_events = reinterpret_cast<ddprof::BPFEvents*>(_ctx);
    bpf_events->_events.push_back(event->cpu_id);
  }
  return 0;
}
#endif

namespace ddprof {

namespace {
DDRes pevent_create(PEventHdr *pevent_hdr, int watcher_idx,
                    size_t *pevent_idx) {
  if (pevent_hdr->size >= pevent_hdr->max_size) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                           "Reached max number of watchers (%lu)",
                           pevent_hdr->max_size);
  }
  *pevent_idx = pevent_hdr->size++;
  pevent_hdr->pes[*pevent_idx].watcher_pos = watcher_idx;
  return {};
}

void pevent_bpf_create(PEventHdr *pevent_hdr,
                       int watcher_pos,
                       int fd){
  pevent_hdr->bpf_pes.push_back({.watcher_pos = watcher_pos,
                                 .fd = fd, .link = nullptr});
  return;
}

void display_system_config() {
  int val;
  DDRes const res = sys_perf_event_paranoid(val);
  if (IsDDResOK(res)) {
    LG_WRN("Check System Configuration - perf_event_paranoid=%d", val);
  } else {
    LG_WRN("Unable to access system configuration");
  }
}

// set info for a perf_event_open type of buffer
void pevent_set_info(int fd, int attr_idx, PEvent &pevent,
                     uint32_t stack_sample_size) {
  static bool log_once = true;
  pevent.fd = fd;
  pevent.mapfd = fd;
  int const buffer_size_order = pevent_compute_min_mmap_order(
      k_default_buffer_size_shift, stack_sample_size,
      k_min_number_samples_per_ring_buffer);
  if (buffer_size_order > k_default_buffer_size_shift && log_once) {
    LG_NTC("Increasing size order of the ring buffer to %d (from %d)",
           buffer_size_order, k_default_buffer_size_shift);
    log_once = false; // avoid flooding for all CPUs
  }
  pevent.ring_buffer_size = perf_mmap_size(buffer_size_order);
  pevent.custom_event = false;
  pevent.ring_buffer_type = RingBufferType::kPerfRingBuffer;
  pevent.attr_idx = attr_idx;
}

DDRes pevent_register_cpu_0(const PerfWatcher *watcher, int watcher_idx,
                            pid_t pid, PerfClockSource perf_clock_source,
                            PEventHdr *pevent_hdr, size_t &pevent_idx) {
  // register cpu 0 and find a working config
  PEvent *pes = pevent_hdr->pes;
  std::vector<perf_event_attr> perf_event_data =
      all_perf_configs_from_watcher(watcher, true, perf_clock_source);
  DDRES_CHECK_FWD(pevent_create(pevent_hdr, watcher_idx, &pevent_idx));

  // attempt with different configs
  for (auto &attr : perf_event_data) {
    // register cpu 0
    int const fd = perf_event_open(&attr, pid, 0, -1, PERF_FLAG_FD_CLOEXEC);
    if (fd != -1) {
      // Copy the successful config
      pevent_hdr->attrs[pevent_hdr->nb_attrs] = attr;
      pevent_set_info(fd, pevent_hdr->nb_attrs, pes[pevent_idx],
                      watcher->options.stack_sample_size);
      ++pevent_hdr->nb_attrs;
      assert(pevent_hdr->nb_attrs <= kMaxTypeWatcher);
      break;
    }
    LG_NFO("Expected failure (we retry with different settings) "
           "perf_event_open for watcher: %s - with attr.type=%s, "
           "exclude_kernel=%d",
           watcher->desc.c_str(), perf_type_str(attr.type),
           static_cast<int>(attr.exclude_kernel));
  }
  // check if one of the configs was successful
  if (pes[pevent_idx].attr_idx == -1) {
    display_system_config();
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                           "Error calling perf_event_open on watcher %d.0 (%s)",
                           watcher_idx, strerror(errno));
  }

  return {};
}

DDRes pevent_open_all_cpus(const PerfWatcher *watcher, int watcher_idx,
                           pid_t pid, int num_cpu,
                           PerfClockSource perf_clock_source,
                           PEventHdr *pevent_hdr) {
  PEvent *pes = pevent_hdr->pes;

  size_t template_pevent_idx = -1;
  DDRES_CHECK_FWD(pevent_register_cpu_0(watcher, watcher_idx, pid,
                                        perf_clock_source, pevent_hdr,
                                        template_pevent_idx));
  int const template_attr_idx = pes[template_pevent_idx].attr_idx;
  perf_event_attr *attr = &pevent_hdr->attrs[template_attr_idx];

  // used the fixed attr for the others
  for (int cpu_idx = 1; cpu_idx < num_cpu; ++cpu_idx) {
    size_t pevent_idx = -1;
    DDRES_CHECK_FWD(pevent_create(pevent_hdr, watcher_idx, &pevent_idx));
    int const fd =
        perf_event_open(attr, pid, cpu_idx, -1, PERF_FLAG_FD_CLOEXEC);
    if (fd == -1) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                             "Error calling perfopen on watcher %d.%d (%s)",
                             watcher_idx, cpu_idx, strerror(errno));
    }
    pevent_set_info(fd, pes[template_pevent_idx].attr_idx, pes[pevent_idx],
                    watcher->options.stack_sample_size);
  }
  return {};
}

} // namespace

void pevent_init(PEventHdr *pevent_hdr) {
  memset(pevent_hdr, 0, sizeof(PEventHdr));
  pevent_hdr->max_size = k_max_nb_perf_event_open;
  for (size_t k = 0; k < pevent_hdr->max_size; ++k) {
    pevent_hdr->pes[k].fd = -1;
    pevent_hdr->pes[k].mapfd = -1;
    pevent_hdr->pes[k].attr_idx = -1;
  }
}

int pevent_compute_min_mmap_order(int min_buffer_size_order,
                                  uint32_t stack_sample_size,
                                  unsigned min_number_samples) {
  int ret_order = min_buffer_size_order;
  // perf events and allocation events should be roughly the same size
  size_t const single_event_size = sizeof_allocation_event(stack_sample_size);
  // Ensure we can at least fit 8 samples within one buffer
  while (((perf_mmap_size(ret_order) - get_page_size()) / single_event_size) <
         min_number_samples) {
    ++ret_order;
  }
  return ret_order;
}

DDRes pevent_open_bpf(DDProfContext &ctx, pid_t pid, int num_cpu,
                      PEventHdr *pevent_hdr) {
  assert(pevent_hdr->size == 0); // check for previous init
  for (unsigned watcher_idx = 0; watcher_idx < ctx.watchers.size();
       ++watcher_idx) {
    PerfWatcher *watcher = &ctx.watchers[watcher_idx];
    if (watcher->type >= kDDPROF_TYPE_CUSTOM
        || watcher->type == PERF_COUNT_SW_DUMMY) {
      // dummy or allocation events should not be managed with bpf
      continue;
    }
    perf_event_attr attr = perf_bpf_config(watcher, false,
                                           ctx.perf_clock_source);
    // used the fixed attr for the others
    for (int cpu_idx = 0; cpu_idx < num_cpu; ++cpu_idx) {
      LG_DBG("Create BPF perf event with %d", pid);
      int const fd = perf_event_open(&attr, pid, cpu_idx, -1, PERF_FLAG_FD_CLOEXEC);
      if (fd == -1) {
        DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                               "Error (BPF flow) calling perf_event_open on watcher %u.%d (%s)",
                               watcher_idx,
                               cpu_idx,
                               strerror(errno));
      }
      pevent_bpf_create(pevent_hdr, watcher_idx, fd);
    }
    // Copy the successful config
    pevent_hdr->attrs[pevent_hdr->nb_attrs] = attr;
    ++pevent_hdr->nb_attrs;
  }
  return {};
}

DDRes pevent_open(DDProfContext &ctx, pid_t pid, int num_cpu,
                  PEventHdr *pevent_hdr) {
  assert(pevent_hdr->size == 0); // check for previous init
  for (unsigned long watcher_idx = 0; watcher_idx < ctx.watchers.size();
       ++watcher_idx) {
    PerfWatcher *watcher = &ctx.watchers[watcher_idx];
    if (watcher->type < kDDPROF_TYPE_CUSTOM) {
      DDRES_CHECK_FWD(pevent_open_all_cpus(watcher, watcher_idx, pid, num_cpu,
                                           ctx.perf_clock_source, pevent_hdr));
    } else {
      // custom event, eg.allocation profiling
      size_t pevent_idx = 0;
      DDRES_CHECK_FWD(pevent_create(pevent_hdr, watcher_idx, &pevent_idx));
      int const order = pevent_compute_min_mmap_order(
          k_mpsc_buffer_size_shift, watcher->options.stack_sample_size,
          k_min_number_samples_per_ring_buffer);
      DDRES_CHECK_FWD(ring_buffer_create(order, RingBufferType::kMPSCRingBuffer,
                                         true, &pevent_hdr->pes[pevent_idx]));
    }
  }
  return {};
}

DDRes pevent_mmap_event(PEvent *event) {
  if (event->mapfd != -1) {
    void *region = perfown_sz(event->mapfd, event->ring_buffer_size);
    if (!region) {
      DDRES_RETURN_ERROR_LOG(
          DD_WHAT_PERFMMAP,
          "Could not mmap memory for watcher #%d: %s. "
          "Please increase kernel limits on pinned memory (ulimit -l). "
          "OR associate the IPC_LOCK capability to this process.",
          event->watcher_pos, strerror(errno));
    }
    if (!rb_init(&event->rb, region, event->ring_buffer_size,
                 event->ring_buffer_type)) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFMMAP,
                             "Could not initialize ring buffer for watcher #%d",
                             event->watcher_pos);
    }
  }
  return {};
}


DDRes pevent_link_bpf(PEventHdr *pevent_hdr) {
#ifdef EBPF_UNWINDING
  // mmaps using ebpf
  assert(pevent_hdr->sample_processor);
  auto *skel = pevent_hdr->sample_processor;
  auto defer_munmap = make_defer([&] { pevent_munmap(pevent_hdr); });

  for (size_t k = 0; k < pevent_hdr->bpf_pes.size(); ++k) {
    pevent_hdr->bpf_pes[k].link =
        bpf_program__attach_perf_event(skel->progs.process_sample, pevent_hdr->bpf_pes[k].fd);
    if (!pevent_hdr->bpf_pes[k].link) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                             "Unable to link bpf program (%lu)", k);
    }
  }
  defer_munmap.release();
#endif
  return {};
}

DDRes pevent_mmap(PEventHdr *pevent_hdr, bool use_override) {
  // Switch user if needed (when root switch to nobody user)
  // Pinned memory is accounted by the kernel by (real) uid across containers
  // (uid 1000 in the host and in containers will share the same count).
  // Sometimes root allowance (when no CAP_IPC_LOCK/CAP_SYS_ADMIN in a
  // container) is already exhausted, hence we switch to a different user.
  UIDInfo info;
  if (use_override) {
    /* perf_event_mlock_kb is accounted per real user id */
    DDRES_CHECK_FWD(user_override_to_nobody_if_root(&info));
  }

  defer {
    if (use_override) {
      user_override(info.uid, info.gid);
    }
  };

  auto defer_munmap = make_defer([&] { pevent_munmap(pevent_hdr); });

  PEvent *pes = pevent_hdr->pes;
  for (size_t k = 0; k < pevent_hdr->size; ++k) {
    DDRES_CHECK_FWD(pevent_mmap_event(&pes[k]));
  }

  defer_munmap.release();

  return {};
}

#ifdef EBPF_UNWINDING
static DDRes pevent_load_bpf(DDProfContext &ctx,
                             PEventHdr *pevent_hdr) {
  // we could chose to have separate maps ?
  sample_processor_bpf *skel = sample_processor_bpf__open_and_load();
  if (skel) {
    LG_DBG("Using sample_processor_bpf to capture events");
    pevent_hdr->sample_processor = skel;
      pevent_hdr->bpf_ring_buf = ring_buffer__new(bpf_map__fd(skel->maps.events),
                                              event_handler,
                                              reinterpret_cast<void *>(&ctx.worker_ctx._bpf_events),
                                              NULL);
    if (!pevent_hdr->bpf_ring_buf) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFRB,
                             "error when allocating ring buffer");

    }
    return {};
  } else {
    LG_DBG("Not able to load sample_processor_bpf");
  }
  // No error here
  DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFRB,
                         "error when loading bpf program");
}
#endif

DDRes pevent_setup(DDProfContext &ctx, pid_t pid, int num_cpu,
                   PEventHdr *pevent_hdr) {
#ifdef EBPF_UNWINDING
  // attempt to load bpf program
  if(IsDDResOK(pevent_load_bpf(ctx, pevent_hdr))) {
    LG_DBG("Success loading BPF program");
    DDRES_CHECK_FWD(pevent_open_bpf(ctx, pid, num_cpu, pevent_hdr));
    // bpf links first to know which we should mmap
    DDRES_CHECK_FWD(pevent_link_bpf(pevent_hdr));
    // slightly hacky addition of dummy
    // todo fix bug with allocation profiler (reorder watchers earlier)
    const PerfWatcher *dummy_watcher = ewatcher_from_str("sDUM");
    ctx.watchers.push_back(*dummy_watcher);
    DDRES_CHECK_FWD(pevent_open_all_cpus(
        &(ctx.watchers.back()),
        static_cast<int>(ctx.watchers.size() - 1),
        pid, num_cpu,
        ctx.perf_clock_source, pevent_hdr));
    // mmap only the relevant configs
    DDRES_CHECK_FWD(pevent_mmap(pevent_hdr, false));
  } else
#endif
  {
    // non ebpf flow
    DDRES_CHECK_FWD(pevent_open(ctx, pid, num_cpu, pevent_hdr));
    if (!IsDDResOK(pevent_mmap(pevent_hdr, true))) {
      LG_NTC("Retrying attachment without user override");
      DDRES_CHECK_FWD(pevent_mmap(pevent_hdr, false));
    }
  }
  return {};
}

DDRes pevent_enable(PEventHdr *pevent_hdr) {
  // Just before we enter the main loop, force the enablement of the perf
  // contexts
  for (size_t i = 0; i < pevent_hdr->size; ++i) {
    if (!pevent_hdr->pes[i].custom_event) {
      DDRES_CHECK_INT(ioctl(pevent_hdr->pes[i].fd, PERF_EVENT_IOC_ENABLE),
                      DD_WHAT_IOCTL, "Error ioctl fd=%d (idx#%zu)",
                      pevent_hdr->pes[i].fd, i);
    }
  }
  return {};
}

DDRes pevent_munmap_event(PEvent *event) {
  if (event->rb.base) {
    if (perfdisown(event->rb.base, event->ring_buffer_size) != 0) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFMMAP,
                             "Error when using perfdisown for watcher #%d",
                             event->watcher_pos);
    }
    event->rb.base = nullptr;
  }
  rb_free(&event->rb);
  return {};
}

/// Clean the mmap buffer
DDRes pevent_munmap(PEventHdr *pevent_hdr) {
  PEvent *pes = pevent_hdr->pes;
  DDRes res{};

  for (size_t k = 0; k < pevent_hdr->size; ++k) {
    DDRes const local_res = pevent_munmap_event(&pes[k]);
    if (!IsDDResOK(local_res)) {
      res = local_res;
    }
  }

#ifdef EBPF_UNWINDING
  for (size_t k = 0; k < pevent_hdr->bpf_pes.size(); ++k) {
    if (pevent_hdr->bpf_pes[k].link) {
        bpf_link__destroy(pevent_hdr->bpf_pes[k].link);
        // TODO: check if this effectively closes the fd ?
        pes[k].fd = -1;
    }
  }
#endif
  return res;
}

DDRes pevent_close_event(PEvent *event) {
  if (event->fd != -1) {
    if (close(event->fd) == -1) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                             "Error when closing fd=%d (watcher #%d) (%s)",
                             event->fd, event->watcher_pos, strerror(errno));
    }
    event->fd = -1;
  }
  if (event->custom_event && event->mapfd != -1) {
    if (close(event->mapfd) == -1) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                             "Error when closing mapfd=%d (watcher #%d) (%s)",
                             event->mapfd, event->watcher_pos, strerror(errno));
    }
  }
  return {};
}

DDRes pevent_close(PEventHdr *pevent_hdr) {
  PEvent *pes = pevent_hdr->pes;
  DDRes res{};
  for (size_t k = 0; k < pevent_hdr->size; ++k) {
    DDRes const local_res = pevent_close_event(&pes[k]);
    if (!IsDDResOK(local_res)) {
      res = local_res;
    }
  }
  pevent_hdr->size = 0;
  return res;
}

bool pevent_include_kernel_events(const PEventHdr *pevent_hdr) {
  for (size_t i = 0; i < pevent_hdr->nb_attrs; ++i) {
    if (pevent_hdr->attrs[i].exclude_kernel == 0) {
      return true;
    }
  }
  return false;
}

DDRes pevent_cleanup(PEventHdr *pevent_hdr) {
  DDRes ret = ddres_init();

  // Cleanup both, storing the error if one was generated
  if (DDRes const ret_tmp = pevent_munmap(pevent_hdr); !IsDDResOK((ret_tmp))) {
    ret = ret_tmp;
  }
  if (DDRes const ret_tmp = pevent_close(pevent_hdr); !IsDDResOK((ret_tmp))) {
    ret = ret_tmp;
  }
#ifdef EBPF_UNWINDING
  if (pevent_hdr->sample_processor) {
    sample_processor_bpf__destroy(pevent_hdr->sample_processor);
  }
  if (pevent_hdr->bpf_ring_buf) {
    ring_buffer__free(pevent_hdr->bpf_ring_buf);
  }
#endif
  return ret;
}
} // namespace ddprof
