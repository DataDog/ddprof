// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <algorithm>
#include <chrono>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "logger.hpp"

struct StringTable {
  std::unordered_map<std::string, size_t> table;

  size_t insert(const std::string &str) {
    auto loc = table.find(str);
    size_t ret;
    if (loc == table.end()) {
      ret = table.size();
      table[str] = ret;
    } else {
      ret = loc->second;
    }
    return ret;
  }

  size_t insert(int n) {
    return insert(std::to_string(n));
  }

  void serialize(nlohmann::json &array) {
    std::map<size_t, const std::string *> aggr;
    for (const auto &elem: table)
      aggr[elem.second] = &elem.first;

    for (const auto &elem: aggr)
      array.emplace_back(*elem.second);
  }
};

struct ThreadFrame {
  const std::string _filename;
  const std::string _package;
  const std::string _class;
  const std::string _method;
  int _line;
};

bool operator<(const ThreadFrame &A, const ThreadFrame &B);

struct ThreadFrameTable {
  std::map<ThreadFrame, size_t> table;

  size_t insert(const ThreadFrame &frame) {
    auto loc = table.find(frame);
    size_t ret;
    if (loc == table.end()) {
      ret = table.size();
      table[frame] = ret;
    } else {
      ret = loc->second;
    }
    return ret;
  }

  void serialize(nlohmann::json &array, StringTable &stab) {
    std::map<size_t, const ThreadFrame*> aggr;
    for (const auto &elem: table)
      aggr[elem.second] = &elem.first;

    for (const auto &f : aggr) {
      array.push_back(nlohmann::json::array());
      array.back()[0] = stab.insert(f.second->_filename);
      array.back()[1] = stab.insert(f.second->_package);
      array.back()[2] = stab.insert(f.second->_class);
      array.back()[3] = stab.insert(f.second->_method);
      array.back()[4] = stab.insert(f.second->_line);
    }
  }
};

struct NoisyNeighborCpu {
  std::vector<uint64_t> time_start;
  std::vector<uint64_t> time_end;
  std::vector<pid_t> pid;
  uint64_t base_ns;

  NoisyNeighborCpu(uint64_t _base) : base_ns{_base} {}

  void pid_on(pid_t p, uint64_t t) {
    if (pid.empty()) {
      pid.push_back(p);
      time_start.push_back(base_ns + t);
      return;
    }

    // Ignore if the state isn't different
    if (pid.back() == p)
      return;

    if (time_end.size() +1 != time_start.size()) {
      LG_ERR("Incorrect time size");
    }

    pid.push_back(p);
    time_end.push_back(base_ns + t);   // Old end time
    time_start.push_back(base_ns + t);
  };

  void pid_off(pid_t p, uint64_t t) {
    if (pid.empty() || (!pid.empty() && pid.back() != p)) {
      time_end.push_back(base_ns + t);
      pid.push_back(p);
      time_start.push_back(base_ns + t);
    }
  };

  void clear() {
    time_start.clear();
    time_end.clear();
    pid.clear();
  }
};

struct NoisyNeighbors {
  std::vector<NoisyNeighborCpu> T;

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

  void pid_on(pid_t p, unsigned cpu, uint64_t t) {
    if (cpu < T.size())
      T[cpu].pid_on(p, t);
  };

  void pid_off(pid_t p, unsigned cpu, uint64_t t) {
    if (cpu < T.size())
      T[cpu].pid_off(p, t);
  }

  nlohmann::json finalize(uint64_t t) {
    nlohmann::json ret{};
    std::vector<std::string> thread_names = {};

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
    {
      auto &noisy = ret["timelines"]["noisyneighbor"] = nlohmann::json::object();
      noisy["labelSchema"] = {"CPU_ID", "PID_A", "PID_B"};
      noisy["frameSchema"] = {"filename", "package", "class", "method", "line"};

      auto &lines = noisy["lines"]["noisy_cpu"]= nlohmann::json::array();
      for (size_t i = 0; i < T.size(); i++) {
        // Iterate through entries on this CPU
        // For any given 4ms period, if there are more than one PID on-CPU, count
        // it as a potential noise violation
        if (T[i].time_end.size() < 1 || T[i].time_start.size() < 2)
          continue;
        for (size_t j = 0; j < T[i].pid.size() - 1; j++) {
          uint64_t end_ns = T[i].time_end[j];
          uint64_t this_pid = T[i].pid[j];
          if (this_pid == 0)
            continue;
          for (size_t k = j + 1; j < T[i].pid.size(); k++) {
            uint64_t start_ns = T[i].time_end[k];
            if ( end_ns + 4000 > start_ns)
              break;
            uint64_t other_pid = T[i].pid[k];
            // Since we're skipping pid 0, we may have come back to this PID.
            // Ignore that case and pid 0
            if (other_pid == 0 || other_pid == this_pid)
              continue;

            // If we're here, we found potentially conflicting PID
            size_t frame_idx = frames.insert({
                                 "unknown.cpp",                                // Filename
                                 "libwhatever.so",                             // Package/DSO
                                 "IHaveNoClass",                               // Class (lol)
                                 "function_" + std::to_string(T[i].pid[j]),    // method name
                                 -1});                                         // Line number
            auto &line = lines.emplace_back(nlohmann::json::object());
            line["startNs"] = end_ns;
            line["endNs"] = start_ns;
            line["labels"] = {stab.insert(i), stab.insert(this_pid), stab.insert(other_pid)};
            line["stack"] = {frame_idx};
            line["state"] = active_idx; // duh?
          }
        }
      }
    }

    // Threads
    {
      auto &thread = ret["timelines"]["threads"] = nlohmann::json::object();
      thread["lines"] = nlohmann::json::object();
      thread["frameSchema"] = {"filename", "package", "class", "method", "line"};

      // Iterate through the CPUs
      for (size_t i = 0; i < T.size(); i++) {
        thread_names.push_back("CPU-" + std::to_string(i));
        auto &lines = thread["lines"][thread_names.back()] = nlohmann::json::array();

        // First, check this CPU to see if it has a better overall start time.
        if (!T[i].time_start.empty() && T[i].time_start[0] < ret["timeRange"]["startNs"])
          ret["timeRange"]["startNs"] = T[i].time_start[0];

        // Iterate through entries on this CPU
        for (size_t j = 0; j < T[i].pid.size(); j++) {
          size_t frame_idx = frames.insert({
                               "unknown.cpp",                                // Filename
                               "libwhatever.so",                             // Package/DSO
                               "IHaveNoClass",                               // Class (lol)
                               "function_" + std::to_string(T[i].pid[j]),    // method name
                               -1});                                         // Line number
          lines[j]["startNs"] = T[i].time_start[j];
          if (j < T[i].time_end.size())
            lines[j]["endNs"] = T[i].time_end[j];
          else
            lines[j]["endNs"] = t;
          lines[j]["state"] = T[i].pid[j] > 0 ? active_idx : idle_idx;
          lines[j]["stack"] = {frame_idx};
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
