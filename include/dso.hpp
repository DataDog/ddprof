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

enum class DsoOrigin : uint8_t { kPerfMmapEvent, kProcMaps };

// DSO definition
class Dso {
public:
  Dso() = default; // invalid element
  // pid, start, end, offset, filename (copied once to avoid creating 3
  // different APIs)
  Dso(pid_t pid, ProcessAddress_t start, ProcessAddress_t end,
      Offset_t offset = 0, std::string &&filename = "", inode_t inode = 0,
      uint32_t prot = PROT_EXEC, DsoOrigin origin = DsoOrigin::kPerfMmapEvent);
  // copy parent and update pid
  // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer)
  Dso(Dso parent, pid_t new_pid) : Dso(std::move(parent)) { _pid = new_pid; }

  // Check if the provided address falls within the provided dso
  bool is_within(ProcessAddress_t addr) const;

  // strict comparison
  friend bool operator==(const Dso &, const Dso &) = default;

  bool intersects(const Dso &o) const;

  std::string to_string() const;
  std::string format_filename() const;

  bool is_executable() const;

  // Adjust as linker can reduce size of mmap
  bool adjust_same(const Dso &o);
  void adjust_end(ProcessAddress_t new_end);
  void adjust_start(ProcessAddress_t new_start);

  // check that o is the same as this except for the size that can be smaller
  bool is_same_or_smaller(const Dso &o) const;
  bool is_same_file(const Dso &o) const;

  size_t size() const { return _end - _start; }
  ProcessAddress_t start() const { return _start; }
  // Beware, end is inclusive !
  ProcessAddress_t end() const { return _end; }

  ProcessAddress_t _start{};
  ProcessAddress_t _end{}; // Beware, end is inclusive !
  Offset_t _offset{};      // file offset
  std::string _filename;   // path as perceived by the user
  inode_t _inode{};
  pid_t _pid{-1};
  uint32_t _prot{};
  mutable FileInfoId_t _id{k_file_info_error};
  DsoType _type{DsoType::kUndef};
  DsoOrigin _origin{DsoOrigin::kPerfMmapEvent};
};

std::ostream &operator<<(std::ostream &os, const Dso &dso);
} // namespace ddprof
