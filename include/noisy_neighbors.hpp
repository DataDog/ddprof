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
    std::map<pid_t, uint64_t> pidtable = {};
    std::vector<std::string> thread_names = {};
    ret["threads"] = nlohmann::json::object();
    ret["timeRange"] = nlohmann::json::object();
    ret["strings"] = nlohmann::json::array();
    ret["frames"] = nlohmann::json::array();
    ret["timeRange"]["endNs"] = t;
    ret["timeRange"]["startNs"] = t;

    // Add some helpful entries to the pidtable
    ret["strings"].push_back(""); // first is empty
    size_t path_idx = ret["strings"].size();
    ret["strings"].push_back("unknown.cpp");
    size_t pkg_idx = ret["strings"].size();
    ret["strings"].push_back("libwhatever.so");
    size_t class_idx = ret["strings"].size();
    ret["strings"].push_back("IHaveNoClass");

    // Later on we'll refactor this properly, but for now just cheat.
    size_t active_idx= ret["strings"].size();
    ret["strings"].push_back("ACTIVE");
    size_t inactive_idx= ret["strings"].size();
    ret["strings"].push_back("INACTIVE");

    for (size_t i = 0; i < T.size(); i++) {
      // Populate placeholders.  We do this without checking size since we
      // want a blank entry for idle cores
      thread_names.push_back("CPU_" + std::to_string(i));
      ret["threads"][thread_names.back()] = nlohmann::json::array();

      // First, check this CPU to see if it has a better start time.
      if (!T[i].time_start.empty() && T[i].time_start[0] < ret["timeRange"]["startNs"])
        ret["timeRange"]["startNs"] = T[i].time_start[0];

      // Iterate through entries
      for (size_t j = 0; j < T[i].pid.size(); j++) {
        pid_t this_pid = T[i].pid[j];
        size_t idx_pid = -1ull;
        auto loc = pidtable.find(this_pid);
        if (loc != pidtable.end()) {
          idx_pid = loc->second;
        } else {
          idx_pid = ret["strings"].size();
          if (!this_pid)
            ret["strings"].push_back("Idle");
          else
            ret["strings"].push_back(std::to_string(this_pid));
          pidtable.insert({this_pid, idx_pid});
        }

        ret["threads"][thread_names.back()][j]["stack"] = nlohmann::json::array();
        ret["threads"][thread_names.back()][j]["stack"][0] = idx_pid;
        ret["threads"][thread_names.back()][j]["startNs"] = T[i].time_start[j];

        if (T[i].time_start.size() == T[i].time_end.size())
          ret["threads"][thread_names.back()][j]["endNs"] = T[i].time_end[j];
        else
          ret["threads"][thread_names.back()][j]["endNs"] = t;

        if (T[i].pid[j] != 0)
          ret["threads"][thread_names.back()][j]["label"] = active_idx;
        else
          ret["threads"][thread_names.back()][j]["label"] = inactive_idx;
      }
    }

    // Now that we've gone through everything, fill in the symbol table

    for (size_t i = 0; i < pidtable.size(); i++) {
      ret["frames"].push_back(nlohmann::json::array());
      ret["frames"][i].push_back(path_idx);
      ret["frames"][i].push_back(pkg_idx);
      ret["frames"][i].push_back(class_idx);
      ret["frames"][i].push_back(i);
      ret["frames"][i].push_back(-1);
    }

    return ret;
  };

  void clear() {
    for (auto &t : T)
      t.clear();
  };
};
