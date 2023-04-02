// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <algorithm>
#include <chrono>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <linux/sched.h>

#include "raw_events.hpp"
#include "timeline/timeline.hpp"
#include "perf.hpp"
#include "logger.hpp"

enum SchedState {
  Unseen,
  Running,
  WaitInterruptible,
  WaitUninterruptible,
  Syscall,
};

// Right now each CPU keeps track of only the last state, which is wrong because the sleeping threads
// have states that may matter.
struct ThreadState {
  pid_t pid = -1;
  std::string comm;
  uint64_t state_begin;
  uint64_t state_end;
  int syscall_number; // If we're in a syscall, which one?
  long state;
};

struct NoisyNeighborCpu {
  ThreadState last_state = {};
  std::vector<ThreadState> complete_states = {};

  // TODO - probably refactor
  void sched_runtime(perf_event_sample *sample) {
    SchedStatRuntime *raw = reinterpret_cast<SchedStatRuntime *>(sample->data_raw);

    if (last_state.pid == raw->pid && last_state.state == SchedState::Running) {
      // Ignore
      if (raw->comm != last_state.comm) {
        last_state.state_end = (base_ns + sample->time) - raw->runtime;
        complete_states.push_back(last_state);

        last_state.comm = raw->comm;
        last_state.state_begin = (base_ns + sample->time) - raw->runtime;
      }
    } else {
      // New state?
      if (last_state.state != SchedState::Unseen) {
        last_state.state_end = (base_ns + sample->time) - raw->runtime;
        complete_states.push_back(last_state);
      }

      // New state
      last_state.pid = raw->pid;
      last_state.comm = raw->comm;
      last_state.state_begin = (base_ns + sample->time) - raw->runtime;
      last_state.state = SchedState::Running;
      last_state.syscall_number = 0;
    }
  }

  void sched_switch(perf_event_sample *sample) {
    SchedSwitch *raw = reinterpret_cast<SchedSwitch *>(sample->data_raw);

    raw->print();

    // Find state, if no previous state make a generic one
    if (last_state.pid != -1) {
      last_state.state_end = base_ns + sample->time;
      if (last_state.comm.empty())
        last_state.comm = raw->prev_comm;
      complete_states.push_back(last_state);
    }

    // Populate new old state
    last_state.pid = raw->next_pid;
    last_state.comm = std::string(raw->next_comm);
    last_state.state = raw->prev_state;
    last_state.state_begin = base_ns + sample->time;
    last_state.syscall_number = 0;
  };

  void syscall_enter(perf_event_sample *sample) {
    RawSysEnter *raw = reinterpret_cast<RawSysEnter*>(sample->data_raw);
    if (last_state.pid != -1) {
      last_state.state_end = base_ns + sample->time;
      complete_states.push_back(last_state);
    }

    last_state.pid = sample->pid;
    last_state.comm = "";
    last_state.state = 0;
    last_state.state_begin = base_ns + sample->time;
    last_state.state = SchedState::Syscall;
    last_state.syscall_number = raw->id;
  }

  void syscall_exit(perf_event_sample *sample) {
    if (last_state.pid != -1) {
      last_state.state_end = base_ns + sample->time;
      complete_states.push_back(last_state);
    }

    last_state.state = SchedState::WaitInterruptible;
    last_state.syscall_number = 0;
  }

  void sched_wakeup(perf_event_sample *sample) {
    SchedWakeup *raw = reinterpret_cast<SchedWakeup *>(sample->data_raw);

    if (last_state.pid != -1) {
      last_state.state_end = base_ns + sample->time;
      complete_states.push_back(last_state);
    }

    last_state.pid = raw->pid;
    last_state.comm = raw->comm;
//    last_state.prio = raw->prio;
    last_state.state = SchedState::Running;
    last_state.syscall_number = 0;
  };

  void sched_migrate(perf_event_sample *sample) {
//    SchedMigrateTask *raw = reinterpret_cast<SchedMigrateTask *>(sample->data_raw);
  };

  void flush(uint64_t t) {
    last_state.state_end = t;
    complete_states.push_back(last_state);

    last_state.pid = -1;
  };

  uint64_t base_ns;

  NoisyNeighborCpu(uint64_t _base) : base_ns{_base} {}

  void clear() {
  }
};

struct NoisyNeighbors {
  std::vector<NoisyNeighborCpu> T;

  void sched_switch(perf_event_sample *sample) {T[sample->cpu].sched_switch(sample);};
  void sched_runtime(perf_event_sample *sample) {T[sample->cpu].sched_runtime(sample);};
  void sched_wakeup(perf_event_sample *sample) {T[sample->cpu].sched_wakeup(sample);};
  void sched_migrate(perf_event_sample *sample) {T[sample->cpu].sched_migrate(sample);};
  void syscall_enter(perf_event_sample *sample) {T[sample->cpu].syscall_enter(sample);};
  void syscall_exit(perf_event_sample *sample) {T[sample->cpu].syscall_exit(sample);};

  NoisyNeighbors(int n) { 
    // Read procfs to get the base time
    uint64_t base_ns = get_uptime_ns();
    for (int i = 0; i < n; i++)
      T.push_back(NoisyNeighborCpu{base_ns});
  }

  uint64_t get_uptime_ns() {
    std::ifstream proc_up("/proc/uptime");

    // Get system uptime in ns
    uint64_t up; proc_up >> up; up *= 1e9;

    // Get current epoch ns to get uptime ns
    //
    {
      using namespace std::chrono;
      up = duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count() - up;
    }
    return up;
  }

  nlohmann::json finalize(uint64_t t) {
    nlohmann::json ret{};
    std::vector<std::string> thread_names = {};

    // Flush final events
    // Before we do any processing, let's flush the final event of each CPU
    // This is necessary on low-utilization systems
    for (auto &cpu : T)
      cpu.flush(t);

    // String table (but don't serialize to JSON yet)
    StringTable stab{};
    stab.insert(""); // Always need an empty
    size_t active_idx = stab.insert("ACTIVE");
    size_t idle_idx = stab.insert("INACTIVE");

    // Frame table
    ThreadFrameTable frames{};

    // Time
    ret["timeRange"] = nlohmann::json::object();
    ret["timeRange"]["endNs"] = t;
    ret["timeRange"]["startNs"] = t;

    // Timelines
    ret["timelines"] = nlohmann::json::object();

    // Noisy neighbor
    if (true) {
      auto &noisy = ret["timelines"]["noisyneighbor"] = nlohmann::json::object();
      noisy["labelSchema"] = {"prev_service", "this_service"};
//      noisy["frameSchema"] = {"filename", "package", "class", "method", "line"};
      noisy["frameSchema"] = {"method", "line"};

      auto &lines = noisy["lines"]["noisy_cpu"]= nlohmann::json::array();
      for (size_t i = 0; i < T.size(); i++) {
        ThreadState default_state = {};
        ThreadState *last_state = &default_state; 
        for (auto &event : T[i].complete_states) {
          uint64_t start_ns = event.state_begin;
          uint64_t end_ns = event.state_end;

          // Do we need to adjust the global time?
          if (ret["timeRange"]["startNs"] > start_ns)
            ret["timeRange"]["startNs"] = start_ns;

          if (last_state->pid != event.pid &&
              event.comm != "sleepytime" &&
              last_state->comm == "sleepytime") {
            // If we're here, we found potentially conflicting PID
            size_t frame_idx = frames.insert({
                                 "pidname_" + event.comm,                      // method name
                                 -1});                                         // Line number
            auto &line = lines.emplace_back(nlohmann::json::object());
            line["startNs"] = last_state->state_end;
            line["endNs"] = end_ns;
            line["labels"] = {
              stab.insert(last_state->comm),
              stab.insert(event.comm),
            };
            line["stack"] = {frame_idx};
            line["state"] = active_idx; // duh?
          }
          last_state = &event;
        }
      }
    }

    // Threads
    {
      auto &thread = ret["timelines"]["threads"] = nlohmann::json::object();
      thread["lines"] = nlohmann::json::object();
//      thread["frameSchema"] = {"filename", "package", "class", "method", "line"};
      thread["frameSchema"] = {"method", "line"};

      // Iterate through the CPUs
      for (size_t i = 0; i < T.size(); i++) {
        thread_names.push_back("CPU-" + std::to_string(i));
        auto &lines = thread["lines"][thread_names.back()] = nlohmann::json::array();

        // Iterate through entries
        for (const auto &event : T[i].complete_states) {
          uint64_t start_ns = event.state_begin;
          uint64_t end_ns = event.state_end;

          // Checks
          if (ret["timeRange"]["startNs"] > start_ns)
            ret["timeRange"]["startNs"] = start_ns;

          if (event.comm != "sleepytime")
            continue;

          // Stash frame
          size_t frame_idx = frames.insert({
                               "function_" + event.comm,                     // method name
                               -1});                                         // Line number


          auto &line = lines.emplace_back(nlohmann::json::object());
          line["startNs"] = start_ns;
          line["endNs"] = end_ns;
          if (event.state == SchedState::Syscall)
            line["state"] = idle_idx;
          else
            line["state"] = active_idx;
          line["stack"] = {frame_idx};
        }
      }
    }

    // Now serialize the intermediates
    ret["strings"] = nlohmann::json::array();
    ret["frames"] = nlohmann::json::array();
    frames.serialize(ret["frames"], stab);
    stab.serialize(ret["strings"]);

    // OK done
    return ret;
  }

  void clear() {
    for (auto &t : T)
      t.clear();
  };
};
