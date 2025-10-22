// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#include "address_bitset.hpp"

#include <cassert>

#include <unlikely.hpp>

namespace ddprof {

namespace {
size_t round_up_to_power_of_two(size_t num) {
  if (num == 0) {
    return num;
  }
  // If num is already a power of two
  if ((num & (num - 1)) == 0) {
    return num;
  }
  // not a power of two
  size_t count = 0;
  while (num) {
    num >>= 1;
    count++;
  }
  return size_t{1} << count;
}
} // namespace

// AddressTable implementation
AddressTable::AddressTable(size_t size)
    : table_size(round_up_to_power_of_two(size)), table_mask(table_size - 1),
      max_capacity(table_size * _max_load_factor_percent / _percent_divisor),
      slots(std::make_unique<std::atomic<uintptr_t>[]>(table_size)) {
  // Initialize all slots to empty
  for (size_t i = 0; i < table_size; ++i) {
    slots[i].store(_empty_slot, std::memory_order_relaxed);
  }
}

AddressBitset::AddressBitset(AddressBitset &&other) noexcept {
  move_from(other);
}

AddressBitset &AddressBitset::operator=(AddressBitset &&other) noexcept {
  if (this != &other) {
    move_from(other);
  }
  return *this;
}

AddressBitset::~AddressBitset() {
  // This should not run unless we are sure that we are no longer running the
  // allocation tracking. This is not enough of a synchronization
  if (_chunk_tables) {
    for (size_t i = 0; i < _k_max_chunks; ++i) {
      AddressTable *table = _chunk_tables[i].load(std::memory_order_relaxed);
      delete table;
    }
  }
  if (_large_alloc_table) {
    AddressTable *table = _large_alloc_table->load(std::memory_order_relaxed);
    delete table;
  }
}

void AddressBitset::move_from(AddressBitset &other) noexcept {
  _lower_bits_ignored = other._lower_bits_ignored;
  _per_table_size = other._per_table_size;
  _chunk_tables = std::move(other._chunk_tables);
  _large_alloc_table = std::move(other._large_alloc_table);

  // Reset the state of 'other'
  other._per_table_size = 0;
}

void AddressBitset::init(size_t table_size) {
  _lower_bits_ignored = _k_max_bits_ignored;

  _per_table_size = table_size ? table_size : _k_default_table_size;

  // Verify _k_max_chunks is a power of 2 (for bitwise modulo optimization)
  static_assert((_k_max_chunks & (_k_max_chunks - 1)) == 0,
                "_k_max_chunks must be a power of 2");

  // Initialize redirect table (Level 1)
  _chunk_tables =
      std::make_unique<std::atomic<AddressTable *>[]>(_k_max_chunks);
  for (size_t i = 0; i < _k_max_chunks; ++i) {
    _chunk_tables[i].store(nullptr, std::memory_order_release);
  }

  // Initialize large allocation table
  _large_alloc_table = std::make_unique<std::atomic<AddressTable *>>();
  _large_alloc_table->store(nullptr, std::memory_order_release);
}

AddressTable *AddressBitset::get_table(uintptr_t addr, uint64_t &out_hash,
                                       bool is_large_alloc,
                                       bool create_if_missing) {
  // Hash once for both chunk selection and slot lookup
  out_hash = compute_full_hash(addr);

  // For large allocations (mmap), use dedicated table to avoid sharding
  std::atomic<AddressTable *> *table_ptr;
  if (is_large_alloc) {
    table_ptr = _large_alloc_table.get();
  } else {
    // Use upper 32 bits for chunk selection (small allocations)
    const size_t chunk_idx = (out_hash >> 32) & (_k_max_chunks - 1);
    table_ptr = &_chunk_tables[chunk_idx];
  }

  AddressTable *table = table_ptr->load(std::memory_order_acquire);

  if (!table && create_if_missing) {
    // Lazy allocation: create table (only for add operations)
    auto *new_table = new AddressTable(_per_table_size);
    AddressTable *expected = nullptr;

    // Use acq_rel: release ensures table construction is visible to other
    // threads, acquire synchronizes with competing allocations
    if (table_ptr->compare_exchange_strong(expected, new_table,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
      // Successfully installed our new table
      table = new_table;
    } else {
      // Another thread beat us to it - use theirs
      delete new_table;
      table = expected;
    }
  }

  return table;
}

bool AddressBitset::add(uintptr_t addr, bool is_large_alloc) {
  assert(addr != _k_empty_slot && addr != _k_deleted_slot);

  // Hash once for both chunk and slot lookup
  uint64_t hash;
  AddressTable *table = get_table(addr, hash, is_large_alloc, true);
  if (!table) {
    return false;
  }

  // Check if table is at max capacity (60% load factor)
  if (table->count.load(std::memory_order_relaxed) >= table->max_capacity) {
    return false; // Table is full
  }

  uint32_t slot = hash_to_slot(hash, table->table_mask);

  // Linear probing to find an empty/deleted slot or the address
  for (size_t probe = 0; probe < _k_max_probe_distance; ++probe) {
    uintptr_t current = table->slots[slot].load(std::memory_order_acquire);

    // If empty or deleted, try to claim it
    if (current == AddressTable::_empty_slot ||
        current == AddressTable::_deleted_slot) {
      uintptr_t expected = current;
      if (table->slots[slot].compare_exchange_strong(
              expected, addr, std::memory_order_acq_rel)) {
        // Successfully inserted
        table->count.fetch_add(1, std::memory_order_relaxed);
        return true;
      }
      // CAS failed, reload and check what's there now
      current = table->slots[slot].load(std::memory_order_acquire);
    }

    // Check if slot already contains our address
    if (current == addr) {
      return false; // Already tracked
    }

    // Slot occupied by different address - probe next slot
    slot = (slot + 1) & table->table_mask;
  }

  // Table is too full or probe distance exceeded
  return false;
}

bool AddressBitset::remove(uintptr_t addr, bool is_large_alloc) {
  assert(addr != _k_empty_slot && addr != _k_deleted_slot);

  // Hash once for both chunk and slot lookup
  uint64_t hash;
  // Don't create table if it doesn't exist - address was never added
  AddressTable *table = get_table(addr, hash, is_large_alloc, false);
  if (!table) {
    return false; // Table doesn't exist, so address was never added
  }

  uint32_t slot = hash_to_slot(hash, table->table_mask);

  // Linear probing to find the address
  for (size_t probe = 0; probe < _k_max_probe_distance; ++probe) {
    uintptr_t current = table->slots[slot].load(std::memory_order_acquire);

    if (current == AddressTable::_empty_slot) {
      // Hit an empty slot - address not in table
      return false;
    }

    if (current == AddressTable::_deleted_slot) {
      // Skip tombstones, continue probing
      slot = (slot + 1) & table->table_mask;
      continue;
    }

    if (current == addr) {
      // Found it - mark as deleted (tombstone)
      if (table->slots[slot].compare_exchange_strong(
              current, AddressTable::_deleted_slot,
              std::memory_order_acq_rel)) {
        table->count.fetch_sub(1, std::memory_order_relaxed);
        return true;
      }
      // CAS failed - someone else modified this slot
      return false;
    }

    // Different address - keep probing
    slot = (slot + 1) & table->table_mask;
  }

  // Probe distance exceeded
  return false;
}

void AddressBitset::clear() {
  if (_chunk_tables) {
    for (size_t chunk_idx = 0; chunk_idx < _k_max_chunks; ++chunk_idx) {
      AddressTable *table =
          _chunk_tables[chunk_idx].load(std::memory_order_acquire);
      if (table) {
        for (size_t i = 0; i < table->table_size; ++i) {
          table->slots[i].store(AddressTable::_empty_slot,
                                std::memory_order_relaxed);
        }
        table->count.store(0, std::memory_order_relaxed);
      }
    }
  }

  // Clear large allocation table
  if (_large_alloc_table) {
    AddressTable *table = _large_alloc_table->load(std::memory_order_acquire);
    if (table) {
      for (size_t i = 0; i < table->table_size; ++i) {
        table->slots[i].store(AddressTable::_empty_slot,
                              std::memory_order_relaxed);
      }
      table->count.store(0, std::memory_order_relaxed);
    }
  }
}

int AddressBitset::count() const {
  if (!_chunk_tables) {
    return 0;
  }

  int total = 0;
  for (size_t i = 0; i < _k_max_chunks; ++i) {
    AddressTable *table = _chunk_tables[i].load(std::memory_order_relaxed);
    if (table) {
      total += table->count.load(std::memory_order_relaxed);
    }
  }

  // Add count from large allocation table
  if (_large_alloc_table) {
    AddressTable *table = _large_alloc_table->load(std::memory_order_relaxed);
    if (table) {
      total += table->count.load(std::memory_order_relaxed);
    }
  }

  return total;
}

int AddressBitset::active_shards() const {
  if (!_chunk_tables) {
    return 0;
  }

  int active = 0;
  for (size_t i = 0; i < _k_max_chunks; ++i) {
    if (_chunk_tables[i].load(std::memory_order_relaxed) != nullptr) {
      ++active;
    }
  }

  // Count large allocation table as a shard if active
  if (_large_alloc_table &&
      _large_alloc_table->load(std::memory_order_relaxed) != nullptr) {
    ++active;
  }

  return active;
}

} // namespace ddprof
