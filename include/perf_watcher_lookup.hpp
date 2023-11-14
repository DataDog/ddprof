// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "perf_ringbuffer.hpp"
#include "perf_watcher.hpp"
#include "pevent.hpp"

#include <sys/types.h>
#include <unordered_map>

namespace ddprof {

struct PEvent {
  PerfWatcher *watcher;
  int fd; // Underlying perf event FD for perf_events, otherwise an eventfd that
          // signals data is available in ring buffer
  int mapfd;               // FD for ring buffer, same as `fd` for perf events
  int cpu;                 // CPU id
  int attr_idx;            // matching perf_event_attr
  size_t ring_buffer_size; // size of the ring buffer
  RingBufferType ring_buffer_type;
  RingBuffer rb;     // metadata and buffers for processing perf ringbuffer
};

class PEventTable {
private:
    PEventTable() {}

    // Lookups
    std::unordered_map<uint64_t, PEvent> id_to_pevent;
    std::unordered_map<int, int> cpu_to_fd;

    // Stashed attrs
    std::vector<perf_event_attr> attrs;

public:
    PEventTable(const PEventTable&) = delete;
    PEventTable& operator=(const PEventTable&) = delete;

    static PEventTable& get_instance() {
      static PEventTable instance;
      return instance;
    }

    PEvent *pevent_from_id(uint64_t id) {
      auto it = id_to_pevent.find(id);
      return (it != id_to_pevent.end()) ? it->second : nullptr;
    }

    bool open_custom_watcher(PerfWatcher &watcher, pid_t pid, PerfClockSource perf_clock_source) {
      PEvent event = {
        fd,
        fd,
        cpu,
        attr_id,
        buffer_size_order,
        RingBufferType::kPerfRingBuffer,
        false,
        {}
      };
      int const order = pevent_compute_min_mmap_order(
          k_mpsc_buffer_size_shift, watcher->options.stack_sample_size,
          k_min_number_samples_per_ring_buffer);
      DDRES_CHECK_FWD(ring_buffer_create(order, RingBufferType::kMPSCRingBuffer,
                                         true, &event));
    }

    bool open_perf_watcher(PerfWatcher &watcher, pid_t pid, PerfClockSource perf_clock_source) {
      std::vector<perf_event_attr> possible_attrs = all_perf_configs_from_watcher(&watcher, true, perf_clock_source);

      // We have a number of configurations and we need to try them on all CPUs. We prefer the earlier configurations,
      // but can failover to the later ones.  If a configuration fails, it should not be used again.  Generally, either
      // all or none of a configuration will work.  If we fail midway through, we take what we can get.  We return
      // false if no configs succeed
      for (int cpu = 0; cpu < num_cpu; ++cpu) {
        auto it = possible_attrs.begin();
        while (it != possible_attrs.end()) {
          int fd = perf_event_open(it, pid, cpu, -1, PERF_FLAG_FD_CLOEXEC);
          if (fd == -1) {
#           warning TODO add error here
            it = possible_attrs.erase(it); // Don't retry this config
          }

          // Get the ID
          uint64_t sample_id = 0;
          if (-1 == ioctl(fd, PERF_EVENT_IOC_ID, &sample_id)) {
            // If I can't get the sample, then I can't use this event.
            LG_WARN("Error getting perf sample ID\n");
            close(fd);
            continue;
          }

          // Store the attr
          int attr_id = attrs.size();
          attrs.push_back(it);

          // Figure out which buffer size to use
          static_bool log_once = true;
          int const buffer_size_order = pevent_compute_min_mmap_order(
              k_default_buffer_size_shift, stack_sample_size,
              k_min_number_samples_per_ring_buffer);
          if (buffer_size_order > k_default_buffer_size_shift && log_once) {
#           warning TODO add more errors here
          }

          // Make a PEvent now for the next part; this will get moved into storage if the next operations are successful, but it can't
          // be moved yet because mapping may still fail
          PEvent event = {
            fd,
            fd,
            cpu,
            attr_id,
            buffer_size_order,
            RingBufferType::kPerfRingBuffer,
            false,
            {}
          };

          // We have enough information to configure the ringbuffer. If this CPU
          // already has a perf event on it, then multiplex the ringbuffer
          auto fd_it = cpu_to_fd.find(cpu);
          if (fd_it != fd_it.end()) {
            // This CPU already has a perf_event ringbuffer, so just use that
            auto cpu_fd = fd_it->second;
            if (ioctl(event->mapfd, PERF_EVENT_IOC_SET_OUTPUT, cpu_fd)) {
#             warning TODO add more errors
            }
            event->mapfd = fd_it->second;
          } else {
            // This CPU does not have a perf_event ringbuffer, so make one
            pevent_mmap_event(event);
            cpu_to_fd[cpu] = event->mapfd;
          }

          // Successful, don't retry anymore!
          id_to_pevent.emplace(sample_id, std::move(event));
        } // try to open
      } // cpu
    }

    bool open_watcher(PerfWatcher &watcher, pid_t pid, PerfClockSource perf_clock_source) {
      if (watcher->type < kDDPROF_TYPE_CUSTOM) {
        ...
      } else {
      }

    }

  DDRes enable_all() {
    // Just before we enter the main loop, force the enablement of the perf
    // contexts
    for (const auto& [_, event] : id_to_pevent) {
      (void)_;
      if (event.watcher->type < kDDPROF_TYPE_CUSTOM) {
#       warning TODO better error
//        DDRES_CHECK_INT(ioctl(event.fd, PERF_EVENT_IOC_ENABLE),
//                        DD_WHAT_IOCTL, "Error ioctl fd=%d (idx#%zu)",
//                        event.fd, i);
      }
    }
    return {};
  }

  DDRes cleanup() {
    DDRes ret = ddres_init();
  
    // Cleanup both, storing the error if one was generated
    for (const auto& [_, event] : id_to_pevent) {
      (void)_;
      if (DDRes const ret_tmp = pevent_munmap_event(event), !IsDDResOK((ret_tmp))) {
        ret = ret_tmp;
      }
      if (DDRes const ret_tmp = pevent_close_event(event), !IsDDResOK((ret_tmp))) {
        ret = ret_tmp;
      }
    }

    // Now let's reset the storage
    id_to_pevent.clear();
    cpu_to_fd.clear();
    attrs.clear();
    
    return ret;
  }

  void pollfd_setup(struct pollfd *pfd, int *pfd_len) {
    // Setup poll() to watch perf_event file descriptors
    for (const auto& [_, event] : id_to_pevent) {
      // NOTE: if fd is negative, it will be ignored
      pfd[i].fd = event.fd;
      pfd[i].events = POLLIN | POLLERR | POLLHUP;
    }
  }

};

} // namespace ddprof
