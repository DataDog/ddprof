// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

extern "C" {
#include "ddprof_defs.h"
}

#include <iostream>
#include <string>
#include <utility>

#include "region_holder.hpp"

// Out of namespace to allow holding it in C object

namespace ddprof {

// DSO definition
class Dso {
public:
  Dso();
  // pid, start, end, offset, filename (copied once to avoid creating 3
  // different APIs)
  Dso(pid_t pid, ElfAddress_t start, ElfAddress_t end, ElfAddress_t pgoff = 0,
      std::string &&filename = "", bool executable = true);
  // copy parent and update pid
  Dso(const Dso &parent, pid_t new_pid) : Dso(parent) { _pid = new_pid; }

  // Check if the provided address falls within the provided dso
  bool is_within(pid_t pid, ElfAddress_t addr) const;
  bool errored() const { return _errored; }
  bool operator<(const Dso &o) const;
  // Avoid use of strict == as we do not consider _end in comparison
  bool operator==(const Dso &o) const = delete;
  // perf gives larger regions than proc maps (keep the largest of them)
  bool same_or_smaller(const Dso &o) const;
  bool intersects(const Dso &o) const;
  std::string to_string() const;
  std::string format_filename() const;
  void flag_error() const { _errored = true; }

  pid_t _pid;
  ElfAddress_t _start;
  ElfAddress_t _end;
  ElfAddress_t _pgoff;
  std::string _filename;
  DsoUID_t _id;
  dso::DsoType _type;
  bool _executable;

  mutable bool _errored;
};

std::ostream &operator<<(std::ostream &os, const Dso &dso);
} // namespace ddprof
