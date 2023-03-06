// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "logger.hpp"

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
  struct SchedStatWait : public RawBasic {
    char comm[16];
    pid_t pid;
    char __unused[4];

    uint64_t delay;

    void print() {
      PRINT_NFO("[SCHED][WAIT] comm=%s pid=%d delay=%lu [ns]", comm, pid, delay);
    }
  };
  struct SchedStatRuntime : public RawBasic {
    char comm[16];
    pid_t pid;
    char __unused[4];

    uint64_t runtime;
    uint64_t vruntime;

    void print() {
      PRINT_NFO("[SCHED][RUNTIME] comm=%s pid=%d runtime=%lu [ns] vruntime=%lu [ns]", comm, pid, runtime, vruntime);
    }
  };
  struct SchedWakeup : public RawBasic {
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
  struct SchedStatIowait : public SchedStatWait {};
  struct SchedStatBlocked : public SchedStatWait {};
  struct SchedStatSleep : public SchedStatWait {};
  struct SchedSchedWakeupNew : public SchedWakeup {};

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
