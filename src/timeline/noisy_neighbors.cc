// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "timeline/noisy_neighbors2.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>

#include "raw_events.hpp"
#include "timeline/timeline.hpp"

void NoisyNeighbors::clear() {
  cpu_on.clear();
  cpu_off.clear();
  for (auto &el : completed_states)
    el.clear();
}

void NoisyNeighbors::process_event(perf_event_sample *sample, const std::string &str) {
  if (str == "sched_switch") {
    sched_switch(sample);
  } else if (str == "sched_stat_runtime") {
    sched_runtime(sample);
  } else if (str == "sched_wakeup") {
    sched_wakeup(sample);
  } else if (str == "sched_migrate_task") {
    sched_migrate(sample);
  } else if(str == "syscall_enter") {
    syscall_enter(sample);
  } else if(str == "syscall_exit") {
    syscall_exit(sample);
  }
}

nlohmann::json NoisyNeighbors::finalize(uint64_t last_time) {
}

*\================================= private ==================================*\
void NoisyNeighbors::sched_switch(perf_event_sample *sample) {
  SchedSwitch *event = reinterpret_cast<SchedSwitch *>(sample->raw_data);

  auto &cpu_state = cpu_on[sample->cpu];

  // If the current on-CPU state is valid, then complete + store the event
  if (cpu_state.pid == event->next_pid) {
    // If we're not switching, then we ignore
  } else if (cpu_state.pid != -1) {
    // TODO put previous scheduler state here???
    cpu_state.end = base_ns + sample->time;
    completed_states[sample->cpu].push_back(cpu_state);

    // Now that it's been stored, write it to off-cpu storage
#error hey you forgot this part
  } else {

  cpu_state.pid = event->next_pid;
  cpu_state.begin = cpu_state.end;
  cpu_state.comm = event->next_comm;
}
void NoisyNeighbors::sched_migrate(perf_event_sample *sample) {
  SchedMigrate *event = reinterpret_cast<SchedMigrate *>(sample->raw_data);
  // If a task is migrated and we don't think it's on-CPU, then just change the
  // task.  Otherwise, we invalidate the on-CPU for the other CPU and let the
  // other transitions sort out the state
  auto &cpu_state = cpu_on[event->orig_cpu];
  if (cpu_state.pid == event->pid) {
    cpu_state.end = base_ns + sample->time;
    completed_states[event->orig_cpu].push_back(cpu_state);
    cpu_state.pid = -1; // invalidate current state
  }

  // TODO what if find fails?
  auto &thread_state = cpu_off[event->pid];
  thread_state.cpu = event->dest_cpu;
}
void NoisyNeighbors::sched_runtime(perf_event_sample *sample) {
  // This just tells us what's on CPU.  If it's different, we stash the old one
  SchedStatRuntime *event = reinterpret_cast<SchedStatRuntime *>(sample->raw_data);
  auto &cpu_state = cpu_on[event->cpu];

  if (cpu_state.pid != event->pid) {
    if (cpu_state.pid != -1) {
      cpu_state.end = base_ns + sample->time - event->runtime; // sub time drift
      completed_states[event->orig_cpu].push_back(cpu_state);
      // Now that it's been stored, write it to off-cpu storage
#error hey you forgot this part
    }

    cpu_state.pid = event->pid;
    cpu_state.begin = cpu_state.end;
    cpu_state.comm = event->comm;
  }
}
void NoisyNeighbors::syscall_enter(perf_event_sample *sample) {}
void NoisyNeighbors::syscall_exit(perf_event_sample *sample) {}

NoisyNeighbors::NoisyNeighbors(int num_cpu) { 
  // Read procfs to get the base time
  uint64_t base_ns = get_uptime_ns();
  for (int i = 0; i < num_cpu; i++)
    completed_states.push_back(std::vector<ThreadStates>{});
  get_uptime_ns();
}

uint64_t NoisyNeighbors::set_uptime_ns() {
  std::ifstream proc_up("/proc/uptime");

  // Get system uptime in ns
  uint64_t up; proc_up >> up; up *= 1e9;

  // Get current epoch ns to get uptime ns
  {
    using namespace std::chrono;
    up = duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count() - up;
  }
  return up;
}
