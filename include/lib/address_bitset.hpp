// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>

namespace ddprof {

// Per-mapping hash table (Level 2)
struct AddressTable {
  static constexpr unsigned kDefaultSize = 512 * 1024; // 512K slots = 4MB
  static constexpr unsigned kMaxProbeDistance =
      64; // not adding an address is OK (though we need to remove)
  static constexpr unsigned kMaxLoadFactorPercent =
      60; // 60% load factor 307200 max addresses (per chunk)
  static constexpr uintptr_t kEmptySlot = 0;
  static constexpr uintptr_t kDeletedSlot = 1;

  unsigned table_size;
  unsigned table_mask;
  unsigned
      max_capacity; // max_capacity = table_size * kMaxLoadFactorPercent / 100
  std::unique_ptr<std::atomic<uintptr_t>[]> slots;
  std::atomic<int> count{0};

  explicit AddressTable(unsigned size);
  ~AddressTable() = default;

  // Delete copy/move operations (non-copyable due to atomic members)
  AddressTable(const AddressTable &) = delete;
  AddressTable &operator=(const AddressTable &) = delete;
  AddressTable(AddressTable &&) = delete;
  AddressTable &operator=(AddressTable &&) = delete;
};

class AddressBitset {
  // Two-level sharded address tracking:
  // Level 1: Fixed redirect table mapping address ranges to tables
  // Level 2: Per-mapping open-addressing hash tables
  // This is NOT signal safe.
  // This should be thread safe.
public:
  // Chunk size: 128MB per chunk (matches typical glibc arena spacing)
  static constexpr uintptr_t kChunkShift = 27; // log2(128MB)
  static constexpr size_t kMaxChunks =
      8192; // 8192 chunks × 128MB = 1TB address space

  // Per-chunk table sizing: 128MB / ~4KB avg allocation = ~32K allocations
  // At 60% load factor, need ~54K slots. Use 64K for headroom.
  constexpr static unsigned _k_default_table_size = 65536;

  // Maximum probe distance before giving up
  constexpr static unsigned _k_max_probe_distance = 32;

  explicit AddressBitset(unsigned table_size = 0) { init(table_size); }
  AddressBitset(AddressBitset &&other) noexcept;
  AddressBitset &operator=(AddressBitset &&other) noexcept;

  AddressBitset(AddressBitset &other) = delete;
  AddressBitset &operator=(AddressBitset &other) = delete;

  ~AddressBitset();

  // returns true if the element was inserted
  // if the table is full, we return false
  bool add(uintptr_t addr);
  // returns true if the element was removed
  bool remove(uintptr_t addr);
  void clear();

  // Get approximate count (for stats/reporting only, not for capacity checks)
  // Aggregates counts from all active tables
  [[nodiscard]] int count() const;

  // Get number of active shards (for stats/reporting)
  [[nodiscard]] int active_shards() const;

  // Get shard index for address (for testing/diagnostics)
  [[nodiscard]] static size_t get_shard_index(uintptr_t addr) {
    return (addr >> kChunkShift) & (kMaxChunks - 1);
  }

  // Initialize with given table size (can be called on default-constructed
  // object)
  void init(unsigned table_size);

private:
  static constexpr unsigned _k_max_bits_ignored = 4;
  static constexpr uintptr_t _k_empty_slot = 0;
  static constexpr uintptr_t _k_deleted_slot =
      1; // Tombstone for deleted entries

  unsigned _lower_bits_ignored;
  unsigned _per_table_size = {};

  // Level 1: Redirect table (maps chunks to tables)
  std::unique_ptr<std::atomic<AddressTable *>[]> _chunk_tables;

  void move_from(AddressBitset &other) noexcept;

  // Get or create table for address
  AddressTable *get_table(uintptr_t addr);

  static constexpr uint64_t kHashMultiplier1 =
      0x9E3779B97F4A7C15ULL; // Golden ratio * 2^64
  static constexpr uint64_t kHashMultiplier2 =
      0x85EBCA77C2B2AE63ULL; // Large prime

  // Hash function: multiply-shift with good mixing
  [[nodiscard]] static uint32_t hash_address(uintptr_t addr,
                                             unsigned lower_bits_ignored,
                                             unsigned table_mask) {
    // Remove alignment bits
    uint64_t h = addr >> lower_bits_ignored;
    // Multiply by large prime (golden ratio * 2^64)
    h *= kHashMultiplier1;
    // Mix upper and lower bits
    h ^= h >> 32;
    h *= kHashMultiplier2;
    h ^= h >> 32;
    return static_cast<uint32_t>(h) & table_mask;
  }
};
} // namespace ddprof
