// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

// Live-allocation snapshot: portable serialisation of LiveAllocation across
// worker resets.
//
// The aggregator state in LiveAllocation refers to per-worker handles
// (libdatadog dictionary pointers, indices into SymbolHdr tables, string_views
// into Process/base-frame caches). None of these survive a worker fork, so
// before a worker restart we capture a fully self-owned snapshot via
// `capture_snapshot`, serialise it through a memfd held by the parent, and
// re-intern everything into the freshly-built tables of the new worker via
// `restore_snapshot`.

#pragma once

#include "ddprof_defs.hpp"
#include "live_allocation.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/types.h>
#include <vector>

namespace ddprof {

struct SymbolHdr;

namespace live_alloc_snapshot {

// Fully self-owned mirror of one FunLoc.
struct FunLocPortable {
  ProcessAddress_t ip{};
  ElfAddress_t elf_addr{};
  uint32_t lineno{};
  std::string fn_name;
  std::string fn_system_name;
  std::string fn_file;
  ElfAddress_t map_low{};
  ElfAddress_t map_high{};
  Offset_t map_offset{};
  std::string map_filename;
  std::string map_build_id;
};

// Fully self-owned mirror of one UnwindOutput.
struct UnwindOutputPortable {
  std::vector<FunLocPortable> locs;
  std::string container_id;
  std::string exe_name;
  std::string thread_name;
  int pid{};
  int tid{};
};

struct AddressEntry {
  uintptr_t addr{};
  int64_t value{};
  uint32_t stack_idx{}; // index into Snapshot::stacks
};

struct PidEntry {
  int watcher_pos{};
  pid_t pid{};
  uint32_t address_conflict_count{};
  uint32_t tracked_address_count{};
  std::vector<AddressEntry> addresses;
};

// Special stack index meaning "stack was dropped during budget enforcement".
// On restore this is materialised as a synthetic single-frame stack pointing
// at the [live-alloc cleared] common frame, ensuring per-PID heap totals are
// preserved even when detail is shed.
inline constexpr uint32_t k_cleared_stack_idx =
    std::numeric_limits<uint32_t>::max();

struct Snapshot {
  std::vector<UnwindOutputPortable> stacks;
  std::vector<PidEntry> pids;
  // How many distinct allocation addresses had their stack remapped to the
  // synthetic cleared stack because of the size budget.
  uint32_t cleared_addresses{};
  // How many (watcher_pos, pid) entries we dropped entirely because even
  // minimal accounting did not fit in the budget.
  uint32_t dropped_pids{};
};

// Default and hard-ceiling sizes for the serialised snapshot.
inline constexpr std::size_t k_default_max_snapshot_bytes = 4UL * 1024 * 1024;
inline constexpr std::size_t k_hard_max_snapshot_bytes = 20UL * 1024 * 1024;

// Read a Snapshot out of an in-memory LiveAllocation, by resolving symbol /
// mapping IDs through `symbol_hdr`. The result has no references to the
// originating worker.
//
// If `max_bytes` is non-zero and the projected serialised size exceeds it,
// the snapshot is degraded in a value-preserving way: low-value stacks are
// remapped to the synthetic cleared stack first, and finally whole pids are
// dropped from the lowest aggregate value upwards. The `cleared_addresses` /
// `dropped_pids` counters in the result report how aggressive the degradation
// had to be.
Snapshot capture_snapshot(const LiveAllocation &live_alloc,
                          const SymbolHdr &symbol_hdr,
                          std::size_t max_bytes = k_default_max_snapshot_bytes);

// Serialise / deserialise a Snapshot to/from a binary blob.
void serialize(const Snapshot &snapshot, std::vector<uint8_t> &out);
bool deserialize(const uint8_t *data, std::size_t size, Snapshot &out);

// Write the binary blob into `fd` (memfd). Truncates fd to the blob size on
// success. On error, returns false and leaves fd in an unspecified state.
bool write_to_fd(int fd, const Snapshot &snapshot);

// Read a snapshot blob out of `fd`. Returns false if fd is empty / unreadable
// / malformed.
bool read_from_fd(int fd, Snapshot &out);

// Re-intern a snapshot into `symbol_hdr` and populate the empty `live_alloc`
// state. Restored UnwindOutputs hold string_views into
// LiveAllocation::_restored_strings.
void restore_snapshot(const Snapshot &snapshot, LiveAllocation &live_alloc,
                      SymbolHdr &symbol_hdr);

} // namespace live_alloc_snapshot

} // namespace ddprof
