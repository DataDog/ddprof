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
#include <unordered_set>
#include <set>
#include <string>


#include <nlohmann/json.hpp>

#include "perf.hpp"
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
      array.back()[0] = stab.insert(f.second->_method);
      array.back()[1] = stab.insert(f.second->_line);
    }
  }
};

#pragma pack(push,1)
  struct RawBasic {
    unsigned short common_type;
    unsigned char common_flags;
    unsigned char common_preempt_count;
    int common_pid;
  };
  struct RawSyscall : public RawBasic {  
    long id;
    unsigned long args[6];
  };
  struct StatWait : public RawBasic {
    char comm[16];
    pid_t pid;
    char __unused[4];

    uint64_t delay;

    void print() {
      PRINT_NFO("[SCHED][WAIT] comm=%s pid=%d delay=%lu [ns]", comm, pid, delay);
    }
  };
  struct StatRuntime : public RawBasic {
    char comm[16];
    pid_t pid;
    char __unused[4];

    uint64_t runtime;
    uint64_t vruntime;

    void print() {
      PRINT_NFO("[SCHED][RUNTIME] comm=%s pid=%d runtime=%lu [ns] vruntime=%lu [ns]", comm, pid, runtime, vruntime);
    }
  };
  struct Wakeup : public RawBasic {
    char comm[16];
    pid_t pid;
    int prio;
    int success;
    int target_cpu;

    void print() {
      PRINT_NFO("[SCHED][WAKEUP] comm=%s pid=%d prio=%d target_cpu=%03d", comm, pid, prio, target_cpu);
    }
  };
  struct SchedSwitch : public RawBasic {
    char prev_comm[16];
    pid_t prev_pid;
    int prev_prio;
    long prev_state;

    char next_comm[16];
    pid_t next_pid;
    int next_prio;

    void print() {
      PRINT_NFO("[SCHED][SWITCH] prev_comm=%s prev_pid=%d prev_prio=%d prev_state=%ld ==> next_comm=%s next_pid=%d next_prio=%d",
          prev_comm, prev_pid, prev_prio, prev_state, next_comm, next_pid, next_prio);
    }
  };
  struct SchedProcessWait : public RawBasic {
    char comm[16];
    pid_t pid;
    int prio;

    void print() {
      PRINT_NFO("[SCHED][WAIT] comm=%s pid=%d prio=%d", comm, pid, prio);
    }
  };
  struct SchedProcessHang : public RawBasic {
    char comm[16];
    pid_t pid;

    void print() {
      PRINT_NFO("[SCHED][WAIT] comm=%s pid=%d", comm, pid);
    }
  };
  struct SchedProcessFork {
    char parent_comm[16];
    pid_t parent_pid;
    char child_comm[16];
    pid_t child_pid;

    void print() {
      PRINT_NFO("[SCHED][FORK] comm=%s pid=%d child_comm=%s child_pid=%d", parent_comm, parent_pid, child_comm, child_pid);
    }
  };
  struct SchedMigrateTask : public RawBasic { 
    char comm[16];
    pid_t pid;
    int prio;
    int orig_cpu;
    int dest_cpu;

    void print() {
      PRINT_NFO("[SCHED][MIGRATE_TASK] comm=%s pid=%d prio=%d orig_cpu=%d dest_cpu=%d", comm, pid, prio, orig_cpu, dest_cpu);
    }
  };

  struct SchedWaitTask : public SchedProcessWait {};
  struct StatIowait : public StatWait {};
  struct StatBlocked : public StatWait {};
  struct StatSleep : public StatWait {};
  struct SchedWakeupNew : public Wakeup {};

  struct ContextSwitch : public RawBasic {
    unsigned int prev_pid;
    unsigned int next_pid;
    unsigned int next_cpu;
    unsigned char prev_prio;
    unsigned char prev_state;
    unsigned char next_prio;
    unsigned char next_state;

    void print() {
      PRINT_NFO("[FTRACE][CONSWITCH] %u:%u:%u  ==> %u:%u:%u [%03u]", prev_pid, prev_prio, prev_state, next_pid, next_prio, next_state, next_cpu);
    }
  };
#pragma pack(pop)

enum class SchedState {
  Unseen,
  Running,
  WaitInterruptible,
  WaitUninterruptible,
  Syscall,
};

// Right now each CPU keeps track of only the last state, which is wrong because the sleeping threads
// have states that may matter.
struct ThreadState {
  SchedState state = SchedState::Unseen;
  pid_t pid = -1;
  std::string comm;
  uint64_t state_begin;
  uint64_t state_end;
  int syscall_number; // If we're in a syscall, which one?
  int prio;
};

struct NoisyNeighborCpu {
  ThreadState last_state = {};
  std::vector<ThreadState> complete_states = {};

  // TODO - probably refactor
  void sched_runtime(perf_event_sample *sample) {
    StatRuntime *raw = reinterpret_cast<StatRuntime *>(sample->data_raw);

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
    last_state.prio = raw->next_prio;
    last_state.state_begin = base_ns + sample->time;
    last_state.syscall_number = 0;
  };

  void syscall_enter(perf_event_sample *sample) {
    RawSyscall *raw = reinterpret_cast<RawSyscall *>(sample->data_raw);
    if (last_state.pid != -1) {
      last_state.state_end = base_ns + sample->time;
      complete_states.push_back(last_state);
    }

    last_state.pid = sample->pid;
    last_state.comm = "";
    last_state.prio = 0;
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
    Wakeup *raw = reinterpret_cast<Wakeup *>(sample->data_raw);

    if (last_state.pid != -1) {
      last_state.state_end = base_ns + sample->time;
      complete_states.push_back(last_state);
    }

    last_state.pid = raw->pid;
    last_state.comm = raw->comm;
    last_state.prio = raw->prio;
    last_state.state = SchedState::Running;
    last_state.syscall_number = 0;
  };

  void sched_migrate(perf_event_sample *sample) {
    SchedMigrateTask *raw = reinterpret_cast<SchedMigrateTask *>(sample->data_raw);
  };

  void flush(uint64_t t) {
    last_state.state_end = t;
    complete_states.push_back(last_state);

    last_state.pid = -1;
  };

  std::vector<uint64_t> time_start = {};
  std::vector<uint64_t> time_end = {};
  std::vector<pid_t> pid = {};
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

//    if (time_end.size() + 1 != time_start.size()) {
//      LG_ERR("Incorrect time size (%ld vs %ld)", time_end.size() + 1, time_start.size());
//    }

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

  void pid_on(pid_t p, unsigned cpu, uint64_t t) {
    T[cpu].pid_on(p, t);
  };

  void pid_off(pid_t p, unsigned cpu, uint64_t t) {
    if (cpu < T.size())
      T[cpu].pid_off(p, t);
  }

  nlohmann::json finalize(uint64_t t) {
    nlohmann::json ret{};
    std::vector<std::string> thread_names = {};

    // Flush final events
    // Before we do any processing, let's flush the final event of each CPU
    // This is necessary on low-utilization systems
    for (auto &cpu : T)
      cpu.flush(t);
//    for (size_t i = 0; i < T.size(); ++i) {
//      T[i].flush(t);
//    }

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


/* Graveyard
  nlohmann::json finalize_old(uint64_t t) {
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
    if (false) {
      auto &noisy = ret["timelines"]["noisyneighbor"] = nlohmann::json::object();
      noisy["labelSchema"] = {"CPU_ID", "PID_A", "PID_B"};
//      noisy["frameSchema"] = {"filename", "package", "class", "method", "line"};
      noisy["frameSchema"] = {"method", "line"};

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
 //                                "unknown.cpp",                                // Filename
 //                                "libwhatever.so",                             // Package/DSO
 //                                "IHaveNoClass",                               // Class (lol)
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
//      thread["frameSchema"] = {"filename", "package", "class", "method", "line"};
      thread["frameSchema"] = {"method", "line"};

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
//                               "unknown.cpp",                                // Filename
//                               "libwhatever.so",                             // Package/DSO
//                               "IHaveNoClass",                               // Class (lol)
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

*/
