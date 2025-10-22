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
  static constexpr size_t _max_probe_distance = 64;
  static constexpr size_t _max_load_factor_percent = 60; // 60% load factor
  static constexpr size_t _percent_divisor = 100;
  static constexpr uintptr_t _empty_slot = 0;
  static constexpr uintptr_t _deleted_slot = 1;

  size_t table_size;
  size_t table_mask;
  size_t max_capacity;

  std::unique_ptr<std::atomic<uintptr_t>[]> slots;
  std::atomic<size_t> count{0};

  explicit AddressTable(size_t size);
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
  // todo@r1viollet: this is no longer a bitset. Rename the class.
  // Not doing so for now to keep the PR readable.
public:
  // Chunk size: 128MB per chunk (matches typical glibc arena spacing)
  static constexpr uintptr_t _k_chunk_shift = 27; // log2(128MB)
  static constexpr size_t _k_max_chunks = 128;
  // Max memory: 128 chunks × 32K slots × 8 bytes = 32 MB
  constexpr static size_t _k_default_table_size = 32768;

  // Maximum probe distance before giving up
  constexpr static size_t _k_max_probe_distance = 32;

  explicit AddressBitset(size_t table_size = 0) { init(table_size); }
  AddressBitset(AddressBitset &&other) noexcept;
  AddressBitset &operator=(AddressBitset &&other) noexcept;

  AddressBitset(AddressBitset &other) = delete;
  AddressBitset &operator=(AddressBitset &other) = delete;

  ~AddressBitset();

  // returns true if the element was inserted
  // if the table is full, we return false
  // is_large_alloc: if true, uses dedicated table for large allocations (mmap)
  //                 if false, uses sharded tables for small allocations
  //                 (malloc/new)
  bool add(uintptr_t addr, bool is_large_alloc = false);
  // returns true if the element was removed
  bool remove(uintptr_t addr, bool is_large_alloc = false);
  void clear();

  // Get approximate count (for stats/reporting only, not for capacity checks)
  // Aggregates counts from all active tables
  [[nodiscard]] int count() const;

  // Get number of active shards (for stats/reporting)
  [[nodiscard]] int active_shards() const;

  // Initialize with given table size (can be called on default-constructed
  // object)
  void init(size_t table_size);

private:
  static constexpr size_t _k_max_bits_ignored = 4;
  static constexpr uintptr_t _k_empty_slot = 0;
  static constexpr uintptr_t _k_deleted_slot = 1; // Tombstone value

  size_t _lower_bits_ignored;
  size_t _per_table_size = {};

  // Level 1: Redirect table (maps chunks to tables)
  std::unique_ptr<std::atomic<AddressTable *>[]> _chunk_tables;

  // Dedicated table for large allocations (mmap/munmap)
  // Avoids excessive sharding for large, scattered allocations
  std::unique_ptr<std::atomic<AddressTable *>> _large_alloc_table;

  void move_from(AddressBitset &other) noexcept;

  // Get or create table for address, returns table and hash for slot lookup
  // is_large_alloc: if true, returns the dedicated large allocation table
  // create_if_missing: if true, creates table if it doesn't exist (for add)
  //                    if false, returns nullptr if table doesn't exist (for
  //                    remove)
  AddressTable *get_table(uintptr_t addr, uint64_t &out_hash,
                          bool is_large_alloc, bool create_if_missing);

  static constexpr uint64_t _k_hash_multiplier_1 =
      0x9E3779B97F4A7C15ULL; // Golden ratio * 2^64
  static constexpr uint64_t _k_hash_multiplier_2 =
      0x85EBCA77C2B2AE63ULL; // Large prime

  // Compute full hash for address (hash once, use for both chunk and slot)
  [[nodiscard]] static uint64_t compute_full_hash(uintptr_t addr) {
    uint64_t h = addr >> _k_max_bits_ignored;
    h *= _k_hash_multiplier_1;
    h ^= h >> 32;
    h *= _k_hash_multiplier_2;
    h ^= h >> 32;
    return h;
  }

  // Extract slot from precomputed hash
  [[nodiscard]] static uint32_t hash_to_slot(uint64_t hash, size_t table_mask) {
    return static_cast<uint32_t>(hash) & table_mask;
  }
};
} // namespace ddprof
