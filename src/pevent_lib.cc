// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "pevent_lib.hpp"

#include "ddprof_cmdline.hpp"
#include "ddres.hpp"
#include "defer.hpp"
#include "perf.hpp"
#include "ringbuffer_utils.hpp"
#include "sys_utils.hpp"
#include "syscalls.hpp"
#include "user_override.hpp"

#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

static DDRes pevent_create(PEventHdr *pevent_hdr, int watcher_idx,
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

void pevent_init(PEventHdr *pevent_hdr) {
  memset(pevent_hdr, 0, sizeof(PEventHdr));
  pevent_hdr->max_size = MAX_NB_PERF_EVENT_OPEN;
  for (size_t k = 0; k < pevent_hdr->max_size; ++k) {
    pevent_hdr->pes[k].fd = -1;
    pevent_hdr->pes[k].mapfd = -1;
    pevent_hdr->pes[k].attr_idx = -1;
  }
}

static void display_system_config(void) {
  int val;
  DDRes res = ddprof::sys_perf_event_paranoid(val);
  if (IsDDResOK(res)) {
    LG_WRN("Check System Configuration - perf_event_paranoid=%d", val);
  } else {
    LG_WRN("Unable to access system configuration");
  }
}

// set info for a perf_event_open type of buffer
static void pevent_set_info(int fd, int attr_idx, PEvent &pevent) {
  pevent.fd = fd;
  pevent.mapfd = fd;
  pevent.ring_buffer_size = perf_mmap_size(DEFAULT_BUFF_SIZE_SHIFT);
  pevent.custom_event = false;
  pevent.ring_buffer_type = RingBufferType::kPerfRingBuffer;
  pevent.attr_idx = attr_idx;
}

static void pevent_add_child_fd(int child_fd, PEvent &pevent) {
  pevent.child_fds[pevent.current_child_fd++] = child_fd;
}

struct LinkedPerfs {
  int seq;
  const std::string tracepoint_group;
  const std::string tracepoint_name;
  bool has_stack;
  bool extras;
};

bool operator==(LinkedPerfs const &A, LinkedPerfs const &B) {
  return A.tracepoint_name == B.tracepoint_name && A.tracepoint_group == B.tracepoint_group;
}

std::unordered_map<long, std::string> id_stash = {};

void stash_perf_id(long id, const std::string &name) {
  id_stash[id] = name;
}

void stash_perf_id(long id, const LinkedPerfs &perf) {
  id_stash[id] = perf.tracepoint_name;
}

const std::string &check_perf_stash(long id) {
  static const std::string empty_str{""};
  auto loc = id_stash.find(id);
  if (loc == id_stash.end())
    return empty_str; 
  else
    return loc->second;
}

using LinkedPerfConf = std::set<LinkedPerfs>;
bool operator<(const LinkedPerfs &A, const LinkedPerfs &B) {
  return A.seq < B.seq;
}

template<>
struct std::hash<LinkedPerfs> {
  std::size_t operator()(LinkedPerfs const &A) const noexcept {
    return std::hash<std::string>{}(A.tracepoint_group + ":" + A.tracepoint_name);
  }
};

static DDRes link_perfs(PerfWatcher *watcher, int watcher_idx, pid_t pid,
                        int num_cpu, PEventHdr *pevent_hdr, const LinkedPerfConf &conf, std::map<int, int> &cpu_pevent_idx) {
  PerfWatcher watcher_copy = *watcher;
  PEvent *pes = pevent_hdr->pes;

  std::unordered_map<LinkedPerfs, int> tracepoint_ids;
  for (auto &probe : conf)
    tracepoint_ids[probe] = -1;

  // Set the IDs
  for (const auto &probe : conf) {
    long id = id_from_tracepoint(probe.tracepoint_group.c_str(), probe.tracepoint_name.c_str());
    if (-1 == id) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                             "Error opening tracefs for %s:%s",
                             probe.tracepoint_group.c_str(),
                             probe.tracepoint_name.c_str());
    }
    tracepoint_ids[probe] = id;
  }

  // The idea is to instrument every probe on every CPU
  // All probes on the same CPU are collected and will be `ioctl()`'d later
  // 

  for (const auto &probe: conf) {
    // Setup the copy of the watcher with tracepoint stuff
    watcher_copy.tracepoint_group = probe.tracepoint_group.c_str();
    watcher_copy.tracepoint_name = probe.tracepoint_name.c_str();
    watcher_copy.config = tracepoint_ids[probe];
    watcher_copy.sample_stack_size = probe.has_stack ? watcher->sample_stack_size / 4 : 0;

    // Record perf_event_open() attr struct
    int attr_idx = pevent_hdr->nb_attrs++;
    perf_event_attr attr = perf_config_from_watcher(&watcher_copy, probe.extras);
    pevent_hdr->attrs[attr_idx] = attr;

    bool watcher_failed = true;
    for (int cpu_idx = 0; cpu_idx < num_cpu; ++cpu_idx) {
      int fd = perf_event_open(&attr, pid, cpu_idx, -1, PERF_FLAG_FD_CLOEXEC);
      if (fd < 0)
        continue;
      watcher_failed = false;

      // Register this sample ID
      uint64_t id;
      if (-1 == ioctl(fd, PERF_EVENT_IOC_ID, &id)) {
        LG_ERR("Error getting perf sample\n");
      }
      stash_perf_id(id, probe);

      if (cpu_pevent_idx.contains(cpu_idx)) {
        pevent_add_child_fd(fd, pes[cpu_pevent_idx[cpu_idx]]);
      } else {
        size_t pevent_idx = -1;
        DDRES_CHECK_FWD(pevent_create(pevent_hdr, watcher_idx, &pevent_idx));
        pevent_set_info(fd, attr_idx, pes[pevent_idx]);
        cpu_pevent_idx[cpu_idx] = pevent_idx;
      }
    }

    // Ideally we'd invalidate the whole group, but we've stashed stuff now
    // TODO fix this
    if (watcher_failed) {
      LG_ERR("Error calling perfopen on (%s:%s)", probe.tracepoint_group.c_str(), probe.tracepoint_name.c_str());
    }
  }
  return ddres_init();
}

static DDRes link_perfs(PerfWatcher *watcher, int watcher_idx, pid_t pid,
                        int num_cpu, PEventHdr *pevent_hdr, const LinkedPerfConf &conf,
                        const std::string &extra_event, std::map<int, int> &cpu_pevent_idx) {
  link_perfs(watcher, watcher_idx, pid, num_cpu, pevent_hdr, conf, cpu_pevent_idx);

  PerfWatcher tmp_watcher = *ewatcher_from_str(extra_event.c_str());
  tmp_watcher.sample_stack_size = watcher->sample_stack_size;
  tmp_watcher.options.is_kernel = watcher->options.is_kernel;
  tmp_watcher.sample_type = watcher->sample_type;
  tmp_watcher.options.no_regs = watcher->options.no_regs;
  PEvent *pes = pevent_hdr->pes;

  // Record perf_event_open() attr struct
  int attr_idx = pevent_hdr->nb_attrs++;
  perf_event_attr attr = perf_config_from_watcher(&tmp_watcher, true); 
  pevent_hdr->attrs[attr_idx] = attr;

  bool watcher_failed = true;
  for (int cpu_idx = 0; cpu_idx < num_cpu; ++cpu_idx) {
    int fd = perf_event_open(&attr, pid, cpu_idx, -1, PERF_FLAG_FD_CLOEXEC);
    if (fd < 0)
      continue;
    watcher_failed = false;

    // Register this sample ID
    uint64_t id;
    if (-1 == ioctl(fd, PERF_EVENT_IOC_ID, &id)) {
      LG_ERR("Error getting perf sample\n");
    }
    stash_perf_id(id, extra_event);

    if (cpu_pevent_idx.contains(cpu_idx)) {
      pevent_add_child_fd(fd, pes[cpu_pevent_idx[cpu_idx]]);
    } else {
      size_t pevent_idx = -1;
      DDRES_CHECK_FWD(pevent_create(pevent_hdr, watcher_idx, &pevent_idx));
      pevent_set_info(fd, attr_idx, pes[pevent_idx]);
      cpu_pevent_idx[cpu_idx] = pevent_idx;
    }
  }

  // Ideally we'd invalidate the whole group, but we've stashed stuff now
  // TODO fix this
  if (watcher_failed)
    LG_ERR("Error calling perfopen on (%s)", extra_event.c_str());
  return ddres_init();
}

static DDRes tallocsys1_open(PerfWatcher *watcher, int watcher_idx, pid_t pid,
                             int num_cpu, PEventHdr *pevent_hdr) {
  const LinkedPerfConf conf = {
        {1, "syscalls", "sys_exit_mmap", true},
        {2, "syscalls", "sys_exit_munmap"},
        {3, "syscalls", "sys_exit_mremap", true}};
  std::map<int, int> cpu_pevent_idx = {};
  return link_perfs(watcher, watcher_idx, pid, num_cpu, pevent_hdr, conf, cpu_pevent_idx);
}

static DDRes topenfd_open(PerfWatcher *watcher, int watcher_idx, pid_t pid,
                             int num_cpu, PEventHdr *pevent_hdr) {
  const LinkedPerfConf conf = {
        {1, "syscalls", "sys_exit_open", true},
        {2, "syscalls", "sys_exit_openat"},
        {3, "syscalls", "sys_exit_close"},
        {4, "syscalls", "sys_exit_exit"},
        {5, "syscalls", "sys_exit_exit_group"}};
  std::map<int, int> cpu_pevent_idx = {};
  return link_perfs(watcher, watcher_idx, pid, num_cpu, pevent_hdr, conf, cpu_pevent_idx);
}

static DDRes tnoisycpu_open(PerfWatcher *watcher, int watcher_idx, pid_t pid,
                           int num_cpu, PEventHdr *pevent_hdr) {
  const LinkedPerfConf conf = {
        {1, "sched", "sched_switch", false, true},
        {2, "sched", "sched_stat_runtime"},
        {3, "sched", "sched_wakeup"},
        {4, "sched", "sched_migrate_task"},
        {5, "raw_syscalls", "sys_enter"},
        {6, "raw_syscalls", "sys_exit"},
  };
  std::map<int, int> cpu_pevent_idx = {};
  return link_perfs(watcher, watcher_idx, pid, num_cpu, pevent_hdr, conf, cpu_pevent_idx);
}

static DDRes tsyscalls_open(PerfWatcher *watcher, int watcher_idx, pid_t pid,
                       int num_cpu, PEventHdr *pevent_hdr) {
  const LinkedPerfConf conf = {
        {1, "raw_syscalls", "sys_enter"},
        {2, "raw_syscalls", "sys_exit"},
  };
  std::map<int, int> cpu_pevent_idx = {};
  return link_perfs(watcher, watcher_idx, pid, num_cpu, pevent_hdr, conf, "sCPU", cpu_pevent_idx);
}


static DDRes pevent_register_cpu_0(const PerfWatcher *watcher, int watcher_idx,
                                   pid_t pid, PEventHdr *pevent_hdr,
                                   size_t &pevent_idx) {
  // register cpu 0 and find a working config
  PEvent *pes = pevent_hdr->pes;
  std::vector<perf_event_attr> perf_event_data =
      ddprof::all_perf_configs_from_watcher(watcher, true);
  DDRES_CHECK_FWD(pevent_create(pevent_hdr, watcher_idx, &pevent_idx));

  // attempt with different configs
  for (auto &attr : perf_event_data) {
    // register cpu 0
    int fd = perf_event_open(&attr, pid, 0, -1, PERF_FLAG_FD_CLOEXEC);
    if (fd != -1) {
      // Copy the successful config
      pevent_hdr->attrs[pevent_hdr->nb_attrs] = attr;
      pevent_set_info(fd, pevent_hdr->nb_attrs, pes[pevent_idx]);
      ++pevent_hdr->nb_attrs;
      assert(pevent_hdr->nb_attrs <= MAX_TYPE_WATCHER);
      break;
    } else {
      LG_NFO("Failed to perf_event_open for watcher: %s - with attr.type=%s, "
             "exclude_kernel=%d",
             watcher->desc, perf_type_str(attr.type),
             static_cast<int>(attr.exclude_kernel));
    }
  }
  // check if one of the configs was successful
  if (pes[pevent_idx].attr_idx == -1) {
    display_system_config();
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                           "Error calling perfopen on watcher %d.0 (%s)",
                           watcher_idx, strerror(errno));
  }

  return ddres_init();
}

static DDRes pevent_open_all_cpus(const PerfWatcher *watcher, int watcher_idx,
                                  pid_t pid, int num_cpu,
                                  PEventHdr *pevent_hdr) {
  PEvent *pes = pevent_hdr->pes;

  size_t template_pevent_idx = -1;
  DDRES_CHECK_FWD(pevent_register_cpu_0(watcher, watcher_idx, pid, pevent_hdr,
                                        template_pevent_idx));
  int template_attr_idx = pes[template_pevent_idx].attr_idx;
  perf_event_attr *attr = &pevent_hdr->attrs[template_attr_idx];

  // used the fixed attr for the others
  for (int cpu_idx = 1; cpu_idx < num_cpu; ++cpu_idx) {
    size_t pevent_idx = -1;
    DDRES_CHECK_FWD(pevent_create(pevent_hdr, watcher_idx, &pevent_idx));
    int fd = perf_event_open(attr, pid, cpu_idx, -1, PERF_FLAG_FD_CLOEXEC);
    if (fd == -1) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                             "Error calling perfopen on watcher %d.%d (%s)",
                             watcher_idx, cpu_idx, strerror(errno));
    }
    pevent_set_info(fd, pes[template_pevent_idx].attr_idx, pes[pevent_idx]);
  }
  return ddres_init();
}

DDRes pevent_open(DDProfContext *ctx, pid_t pid, int num_cpu,
                  PEventHdr *pevent_hdr) {
  assert(pevent_hdr->size == 0); // check for previous init
  for (int watcher_idx = 0; watcher_idx < ctx->num_watchers; ++watcher_idx) {
    PerfWatcher *watcher = &ctx->watchers[watcher_idx];
    if (watcher->instrument_self) {
      // Here we inline a lookup for the specific handler, but in reality this
      // should be defined at the level of the watcher
      switch (watcher->ddprof_event_type) {
      case DDPROF_PWE_tALLOCSYS1:
        DDRES_CHECK_FWD(
            tallocsys1_open(watcher, watcher_idx, pid, num_cpu, pevent_hdr));
        break;
      case (DDPROF_PWE_tNOISYCPU):
        DDRES_CHECK_FWD(
            tnoisycpu_open(watcher, watcher_idx, pid, num_cpu, pevent_hdr));
        break;
      case (DDPROF_PWE_tSYSCALLS):
        DDRES_CHECK_FWD(
            tsyscalls_open(watcher, watcher_idx, pid, num_cpu, pevent_hdr));
        break;
      case DDPROF_PWE_tOPENFD:
        DDRES_CHECK_FWD(
            topenfd_open(watcher, watcher_idx, pid, num_cpu, pevent_hdr));
        break;
      }
    } else if (watcher->type < kDDPROF_TYPE_CUSTOM) {
      DDRES_CHECK_FWD(
          pevent_open_all_cpus(watcher, watcher_idx, pid, num_cpu, pevent_hdr));
    } else {
      // custom event, eg.allocation profiling
      size_t pevent_idx = 0;
      DDRES_CHECK_FWD(pevent_create(pevent_hdr, watcher_idx, &pevent_idx));
      DDRES_CHECK_FWD(ddprof::ring_buffer_create(
          MPSC_BUFF_SIZE_SHIFT, RingBufferType::kMPSCRingBuffer, true,
          &pevent_hdr->pes[pevent_idx]));
    }
  }
  return ddres_init();
}

DDRes pevent_mmap_event(PEvent *event) {
  if (event->mapfd != -1) {
    void *region = perfown_sz(event->mapfd, event->ring_buffer_size);
    if (!region) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFMMAP,
                             "Could not mmap memory for watcher #%d: %s",
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

DDRes pevent_setup(DDProfContext *ctx, pid_t pid, int num_cpu,
                   PEventHdr *pevent_hdr) {
  DDRES_CHECK_FWD(pevent_open(ctx, pid, num_cpu, pevent_hdr));
  if (!IsDDResOK(pevent_mmap(pevent_hdr, true))) {
    LG_NTC("Retrying attachment without user override");
    DDRES_CHECK_FWD(pevent_mmap(pevent_hdr, false));
  }

  // If any watchers have self-instrumentation, then they may have set up child
  // fds which now need to be consolidated via ioctl.  These fds cannot be
  // closed until profiling is completed.
  for (unsigned i = 0; i < pevent_hdr->size; i++) {
    PEvent *pes = &pevent_hdr->pes[i];
    if ( ctx->watchers[pes->watcher_pos].instrument_self) {
      int fd = pes->fd;
      for (int j = 0; j < pes->current_child_fd; ++j) {
        int child_fd = pes->child_fds[j];
        if (ioctl(child_fd, PERF_EVENT_IOC_SET_OUTPUT, fd)) {
          DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFOPEN,
                                 "Could not ioctl() linked perf buffers");
        }
      }
    }
  }
  return ddres_init();
}

DDRes pevent_enable(PEventHdr *pevent_hdr) {
  // Just before we enter the main loop, force the enablement of the perf
  // contexts
  for (size_t i = 0; i < pevent_hdr->size; ++i) {
    PEvent *pes = &pevent_hdr->pes[i];
    if (!pes->custom_event) {
      DDRES_CHECK_INT(ioctl(pevent_hdr->pes[i].fd, PERF_EVENT_IOC_ENABLE),
                      DD_WHAT_IOCTL, "Error ioctl fd=%d (idx#%zu)",
                      pevent_hdr->pes[i].fd, i);
      for (int j = 0; j < pes->current_child_fd; ++j) {
        int child_fd = pes->child_fds[j];
        ioctl(child_fd, PERF_EVENT_IOC_ENABLE);
      }
    } else {
      PRINT_NFO("SKIPPING DUE TO CUSTOM EVENT");
    }
  }
  return ddres_init();
}

DDRes pevent_munmap_event(PEvent *event) {
  if (event->rb.base) {
    if (perfdisown(event->rb.base, event->ring_buffer_size) != 0) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PERFMMAP,
                             "Error when using perfdisown for watcher #%d",
                             event->watcher_pos);
    }
    event->rb.base = NULL;
  }
  rb_free(&event->rb);
  return {};
}

/// Clean the mmap buffer
DDRes pevent_munmap(PEventHdr *pevent_hdr) {
  PEvent *pes = pevent_hdr->pes;
  DDRes res{};

  for (size_t k = 0; k < pevent_hdr->size; ++k) {
    DDRes local_res = pevent_munmap_event(&pes[k]);
    if (!IsDDResOK(local_res)) {
      res = local_res;
    }
  }

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
    DDRes local_res = pevent_close_event(&pes[k]);
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
  DDRes ret_tmp;

  // Cleanup both, storing the error if one was generated
  if (!IsDDResOK(ret_tmp = pevent_munmap(pevent_hdr)))
    ret = ret_tmp;
  if (!IsDDResOK(ret_tmp = pevent_close(pevent_hdr)))
    ret = ret_tmp;
  return ret;
}
