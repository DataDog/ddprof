// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"

#include "ddprof_file_info-i.hpp"
#include "dso_type.hpp"

#include <iostream>
#include <string>
#include <sys/mman.h>
#include <utility>

// Out of namespace to allow holding it in C object

namespace ddprof {

// DSO definition
class Dso {
public:
  Dso();
  // pid, start, end, offset, filename (copied once to avoid creating 3
  // different APIs)
  Dso(pid_t pid, ElfAddress_t start, ElfAddress_t end, ElfAddress_t pgoff = 0,
      std::string &&filename = "", inode_t inode = 0,
      uint32_t prot = PROT_EXEC);
  // copy parent and update pid
  Dso(const Dso &parent, pid_t new_pid) : Dso(parent) { _pid = new_pid; }

  // Check if the provided address falls within the provided dso
  bool is_within(ElfAddress_t addr) const;
  // Avoid use of strict == as we do not consider _end in comparison
  bool operator==(const Dso &o) const = delete;
  // perf gives larger regions than proc maps (keep the largest of them)

  bool intersects(const Dso &o) const;

  std::string to_string() const;
  std::string format_filename() const;

  bool is_executable() const;

  // Adjust as linker can reduce size of mmap
  bool adjust_same(const Dso &o);
  size_t size() const { return _end - _start; }

  pid_t _pid;
  ElfAddress_t _start;
  ElfAddress_t _end;
  ElfAddress_t _pgoff;
  std::string _filename; // path as perceived by the user
  inode_t _inode;
  uint32_t _prot;
  dso::DsoType _type;
  mutable FileInfoId_t _id;

private:
  static bool is_jit_dump_str(std::string_view file_path, pid_t pid);
};

std::ostream &operator<<(std::ostream &os, const Dso &dso);
} // namespace ddprof
