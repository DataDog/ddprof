// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddres_def.hpp"
#include "sdt_probe.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ddprof {

/// Configuration for a single uprobe attachment
struct UprobeConfig {
  std::string binary_path; // Path to target binary
  uint64_t offset;         // Offset within binary (from start of file)
  bool is_return_probe;    // true for uretprobe
  pid_t pid;               // Target PID (-1 for all processes)
  uint32_t stack_sample_size; // Size of user stack to capture
};

/// Represents an attached uprobe
struct UprobeAttachment {
  int fd;                    // perf_event fd
  UprobeConfig config;       // Configuration used
  const SDTProbe *probe;     // Associated SDT probe (may be null)
  SDTProbeType probe_type;   // Type of probe for event processing
};

/// Attaches uprobes to SDT probe locations using perf_event_open
class UprobeAttacher {
public:
  UprobeAttacher();
  ~UprobeAttacher();

  // Non-copyable
  UprobeAttacher(const UprobeAttacher &) = delete;
  UprobeAttacher &operator=(const UprobeAttacher &) = delete;

  // Movable
  UprobeAttacher(UprobeAttacher &&other) noexcept;
  UprobeAttacher &operator=(UprobeAttacher &&other) noexcept;

  /// Get the uprobe PMU type from sysfs
  /// Returns nullopt if uprobes are not supported
  std::optional<uint32_t> get_uprobe_type();

  /// Attach a single uprobe
  /// @param config Uprobe configuration
  /// @param out Output attachment info
  /// @return DDRes indicating success or failure
  DDRes attach(const UprobeConfig &config, UprobeAttachment *out);

  /// Attach all allocation-related SDT probes from a binary
  /// @param probes SDT probe set discovered from the binary
  /// @param pid Target process ID
  /// @param stack_sample_size Size of user stack to capture
  /// @param out Vector to store attachment info
  /// @return DDRes indicating success or failure
  DDRes attach_allocation_probes(const SDTProbeSet &probes, pid_t pid,
                                 uint32_t stack_sample_size,
                                 std::vector<UprobeAttachment> *out);

  /// Get all current attachments
  const std::vector<UprobeAttachment> &attachments() const {
    return _attachments;
  }

  /// Detach and close all attached uprobes
  void detach_all();

  /// Enable all attached uprobes
  DDRes enable_all();

  /// Disable all attached uprobes
  DDRes disable_all();

private:
  std::optional<uint32_t> _uprobe_type;
  std::vector<UprobeAttachment> _attachments;
};

/// Read the uprobe PMU type from sysfs
/// Returns nullopt if not available
std::optional<uint32_t> read_uprobe_pmu_type();

/// Convert a virtual address from an ELF file to a file offset
/// This is needed because uprobes use file offsets, not virtual addresses
/// @param binary_path Path to the binary
/// @param vaddr Virtual address from SDT probe
/// @param offset Output file offset
/// @return DDRes indicating success or failure
DDRes vaddr_to_file_offset(const char *binary_path, uint64_t vaddr,
                           uint64_t *offset);

} // namespace ddprof
