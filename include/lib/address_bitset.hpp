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
  static constexpr unsigned kDefaultSize = 1024 * 1024; // 1M slots = 8MB
  static constexpr unsigned kMaxProbeDistance = 128;
  static constexpr uintptr_t kEmptySlot = 0;
  static constexpr uintptr_t kDeletedSlot = 1;

  unsigned table_size;
  unsigned table_mask;
  std::unique_ptr<std::atomic<uintptr_t>[]> slots;
  std::atomic<int> count{0};

  explicit AddressTable(unsigned size);
  ~AddressTable() = default;
  
  // Delete copy/move operations (non-copyable due to atomic members)
  AddressTable(const AddressTable&) = delete;
  AddressTable& operator=(const AddressTable&) = delete;
  AddressTable(AddressTable&&) = delete;
  AddressTable& operator=(AddressTable&&) = delete;
};

class AddressBitset {
  // Two-level sharded address tracking:
  // Level 1: Fixed redirect table mapping address ranges to tables
  // Level 2: Per-mapping open-addressing hash tables
  // Signal-safe after initialization: only atomic operations
public:
  // Chunk size: 4GB per chunk (reasonable for typical address space usage)
  static constexpr uintptr_t kChunkShift = 32; // log2(4GB)
  static constexpr size_t kMaxChunks = 256; // 256 chunks × 4GB = 1TB address space
  static constexpr size_t kTablesPerAllocation = 8; // Expect ~8 active tables

  // Default capacity per table
  constexpr static unsigned _k_default_table_size = 8* 1024 * 1024;
  // Maximum probe distance before giving up
  constexpr static unsigned _k_max_probe_distance = 128;

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
  [[nodiscard]] int count() const { 
    return _total_count.load(std::memory_order_relaxed); 
  }

private:
  static constexpr unsigned _k_max_bits_ignored = 4;
  static constexpr uintptr_t _k_empty_slot = 0;
  static constexpr uintptr_t _k_deleted_slot = 1; // Tombstone for deleted entries

  unsigned _lower_bits_ignored;
  unsigned _per_table_size = {};

  // Level 1: Redirect table (maps chunks to tables)
  std::unique_ptr<std::atomic<AddressTable*>[]> _chunk_tables;
  
  // Global count for stats (not used for capacity checks)
  std::atomic<int> _total_count{0};

  void init(unsigned table_size);
  void move_from(AddressBitset &other) noexcept;

  // Get or create table for address
  AddressTable* get_table(uintptr_t addr);

  static constexpr uint64_t kHashMultiplier1 = 0x9E3779B97F4A7C15ULL; // Golden ratio * 2^64
  static constexpr uint64_t kHashMultiplier2 = 0x85EBCA77C2B2AE63ULL; // Large prime

  // Hash function: multiply-shift with good mixing
  [[nodiscard]] static uint32_t hash_address(uintptr_t addr, unsigned lower_bits_ignored, unsigned table_mask) {
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
