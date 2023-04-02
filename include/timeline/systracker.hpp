// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <nlohmann/json.hpp>
#include <fstream>
#include <unordered_map>
#include <vector>

#include "raw_events.hpp"
#include "timeline/timeline.hpp"
#include "logger.hpp"
#include "x86_syscalls.hpp"

struct ThreadEvents {
  int syscall_number = -1;
  uint64_t state_begin;
  uint64_t state_end;
  bool failed; // syscall failure?
  bool finished;
};

class Systracker {
  uint64_t base_ns;
  std::unordered_map<pid_t, std::vector<ThreadEvents>> thread_events;
  std::unordered_map<pid_t, std::string> commtable;
  uint64_t get_uptime_ns() {
    // Get current epoch ns to get uptime ns
    std::ifstream proc_up("/proc/uptime");
    uint64_t up; proc_up >> up; up *= 1e9;
    {
      using namespace std::chrono;
      up = duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count() - up;
    }
    return up;
  }

  void cpu_on(perf_event_sample *sample) {
    if (thread_events[sample->pid].size()) {
      auto &state = thread_events[sample->pid].back();
      if (!state.finished)
        state.state_end = base_ns + sample->time; 
    }
    thread_events[sample->pid].push_back({});
    auto &state = thread_events[sample->pid].back();
    state.state_begin = base_ns + sample->time - sample->period; 
    state.state_end = base_ns + sample->time; 
    state.finished = true;
    state.failed = false;
    state.syscall_number = -1;
  }

  void sys_enter(perf_event_sample *sample) {
    RawSysEnter *event = reinterpret_cast<RawSysEnter *>(sample->data_raw);

    thread_events[sample->pid].push_back({});
    auto &state = thread_events[sample->pid].back();
    state.state_begin = base_ns + sample->time; 
    state.state_end = base_ns + sample->time; 
    state.syscall_number = event->id;
  }
  void sys_exit(perf_event_sample *sample) {
    RawSysExit *event = reinterpret_cast<RawSysExit *>(sample->data_raw);

    if (thread_events[sample->pid].size()) {
      auto &state = thread_events[sample->pid].back();
      state.state_end = base_ns + sample->time; 
      if (state.syscall_number == event->id) {
        if (event->ret > -4096UL)
          state.failed = true;
        else
          state.failed= false;
      }
    }
  }
  

public:
  Systracker() {
    base_ns = get_uptime_ns();
  }
  void process_event(perf_event_sample *sample, const std::string_view &sv) {
    if (sv == "sys_enter")
      return sys_enter(sample);
    if (sv == "sys_exit")
      return sys_exit(sample);
    if (sv == "sCPU")
      return cpu_on(sample);
  };

  void set_comm(const perf_event_comm *comm) {
    commtable[comm->pid] = comm->comm;
  };

  nlohmann::json finalize(uint64_t t) {
    nlohmann::json ret{};
    std::vector<std::string> thread_names = {};

    // String table (but don't serialize to JSON yet)
    StringTable stab{};
    stab.insert(""); // Always need an empty
    size_t active_idx = stab.insert("CPU");

    // Frame table
    ThreadFrameTable frames{};

    // Time
    ret["timeRange"] = nlohmann::json::object();
    auto &json_end = ret["timeRange"]["endNs"] = 0;
    auto &json_start = ret["timeRange"]["startNs"] = 0;

    // Timelines
    ret["timelines"] = nlohmann::json::object();

    // Threads
    {
      auto &thread = ret["timelines"]["threads"] = nlohmann::json::object();
      thread["lines"] = nlohmann::json::object();
      thread["frameSchema"] = {"method", "line"};

      for (const auto &events : thread_events) {
        std::string threadname;
        if (auto search = commtable.find(events.first); search != commtable.end())
          threadname = "<" + std::to_string(events.first) + ">" + search->second;
        else
          threadname = "<" + std::to_string(events.first) + ">";
        auto &lines = thread["lines"][threadname] = nlohmann::json::array();

        // Iterate through entries
        for (const auto &event : events.second) {
          uint64_t start_ns = event.state_begin;
          uint64_t end_ns = event.state_end;
          if (end_ns <= start_ns || end_ns - start_ns < 1000)
            continue;
          auto &line = lines.emplace_back(nlohmann::json::object());

          size_t frame_idx = frames.insert({
                        "fun",
                        -1});


          if (0 == json_start || json_start > start_ns)
            json_start = start_ns;
          if (0 == json_end || json_end < end_ns)
            json_end = end_ns;
          line["startNs"] = start_ns;
          line["endNs"] = end_ns;

          line["state"] = event.syscall_number > -1 ? stab.insert(get_syscall(event.syscall_number)) : active_idx;
          line["labels"] = {
            stab.insert("Foo"),
            stab.insert("Bar"),
          };
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
    thread_events.clear();
  }

};
