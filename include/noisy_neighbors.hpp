// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <algorithm>
#include <vector>
#include <unordered_map>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

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

  void pid_on(pid_t p, uint64_t t) {
    if (!pid.empty() && pid.back() == p)
      return; // nothing to do
    else if (!pid.empty() && pid.back() != p && pid.back() != 0) {
      // Replacing a PID; invalidate old one
      time_end.push_back(t);
    }

    pid.push_back(p);
    time_start.push_back(t);
  };

  void pid_off(pid_t p, uint64_t t) {
    // 1. p is not the latest PID, we missed something.  Invalidate the old one
    // 2. p is the latest PID, invalidate it
    // Either way it's the same, as long as the old PID isn't 0
    if (!pid.empty() && pid.back() != p) {
      time_end.push_back(t);
      pid.push_back(0);
      time_start.push_back(t);
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
    for (int i = 0; i < n; i++)
      T.push_back(NoisyNeighborCpu{});
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
    ret["timelines"]["gc"] = nlohmann::json::object();
    ret["timelines"]["stw"] = nlohmann::json::object();

    // Noisy neighbor
    {
      auto &noisy = ret["timelines"]["noisyneighbor"] = nlohmann::json::object();
      noisy["labelSchema"] = {"PID", "CPU_PCT_USED"};
    }

    // Threads
    {
      auto &thread = ret["timelines"]["threads"] = nlohmann::json::object();
      thread["lines"] = nlohmann::json::object();
      thread["frameSchema"] = {"filename", "package", "class", "method", "line"};

      // Iterate through the CPUs
      for (size_t i = 0; i < T.size(); i++) {
        thread_names.push_back("thread-" + std::to_string(i));
        auto &this_thread = thread["lines"][thread_names.back()] = nlohmann::json::array();

        // First, check this CPU to see if it has a better overall start time.
        if (!T[i].time_start.empty() && T[i].time_start[0] < ret["timeRange"]["startNs"])
          ret["timeRange"]["startNs"] = T[i].time_start[0];

        // Iterate through entries on this CPU
        for (size_t j = 0; j < T[i].pid.size(); j++) {
          size_t frame_idx = frames.insert({"unknown.cpp", "libwhatever.so", "IHaveNoClass", "function_" + std::to_string(T[i].pid[j]), -1});
          this_thread[j]["startNs"] = T[i].time_start[j];
          this_thread[j]["endNs"] = T[i].time_start.size() == T[i].time_end.size() ? T[i].time_end[j] : t;
          this_thread[j]["state"] = T[i].pid[j] > 0 ? active_idx : idle_idx;
          this_thread[j]["stack"] = {frame_idx};
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
