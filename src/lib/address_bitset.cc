// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#include "address_bitset.hpp"

#include <unlikely.hpp>

namespace ddprof {

namespace {
unsigned round_up_to_power_of_two(unsigned num) {
  if (num == 0) {
    return num;
  }
  // If num is already a power of two
  if ((num & (num - 1)) == 0) {
    return num;
  }
  // not a power of two
  unsigned count = 0;
  while (num) {
    num >>= 1;
    count++;
  }
  return 1 << count;
}
} // namespace

// AddressTable implementation
AddressTable::AddressTable(unsigned size)
    : table_size(round_up_to_power_of_two(size)), table_mask(table_size - 1),
      max_capacity(table_size * kMaxLoadFactorPercent / kPercentDivisor),
      slots(std::make_unique<std::atomic<uintptr_t>[]>(table_size)) {
  // Initialize all slots to empty
  for (unsigned i = 0; i < table_size; ++i) {
    slots[i].store(kEmptySlot, std::memory_order_relaxed);
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
    for (size_t i = 0; i < kMaxChunks; ++i) {
      AddressTable *table = _chunk_tables[i].load(std::memory_order_relaxed);
      delete table;
    }
  }
}

void AddressBitset::move_from(AddressBitset &other) noexcept {
  _lower_bits_ignored = other._lower_bits_ignored;
  _per_table_size = other._per_table_size;
  _chunk_tables = std::move(other._chunk_tables);

  // Reset the state of 'other'
  other._per_table_size = 0;
}

void AddressBitset::init(unsigned table_size) {
  _lower_bits_ignored = _k_max_bits_ignored;

  _per_table_size = table_size ? table_size : _k_default_table_size;

  // Initialize redirect table (Level 1)
  _chunk_tables = std::make_unique<std::atomic<AddressTable *>[]>(kMaxChunks);
  for (size_t i = 0; i < kMaxChunks; ++i) {
    _chunk_tables[i].store(nullptr, std::memory_order_release);
  }
}

AddressTable *AddressBitset::get_table(uintptr_t addr, uint64_t &out_hash) {
  // Hash once for both chunk selection and slot lookup
  out_hash = compute_full_hash(addr);
  
  // Use upper 32 bits for chunk selection
  const size_t chunk_idx = (out_hash >> 32) % kMaxChunks;

  AddressTable *table =
      _chunk_tables[chunk_idx].load(std::memory_order_acquire);

  if (!table) {
    // Lazy allocation: create table for this chunk
    auto *new_table = new AddressTable(_per_table_size);
    AddressTable *expected = nullptr;

    // Use acq_rel: release ensures table construction is visible to other
    // threads, acquire synchronizes with competing allocations
    if (_chunk_tables[chunk_idx].compare_exchange_strong(
            expected, new_table, std::memory_order_acq_rel,
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

bool AddressBitset::add(uintptr_t addr) {
  if (addr == _k_empty_slot || addr == _k_deleted_slot) {
    return false; // Can't track sentinel values
  }

  // Hash once for both chunk and slot lookup
  uint64_t hash;
  AddressTable *table = get_table(addr, hash);
  if (!table) {
    return false;
  }

  // Check if table is at max capacity (60% load factor)
  if (table->count.load(std::memory_order_relaxed) >=
      static_cast<int>(table->max_capacity)) {
    return false; // Table is full
  }

  uint32_t slot = hash_to_slot(hash, table->table_mask);

  // Linear probing to find an empty/deleted slot or the address
  for (unsigned probe = 0; probe < AddressTable::kMaxProbeDistance; ++probe) {
    uintptr_t current = table->slots[slot].load(std::memory_order_acquire);

    // If empty or deleted, try to claim it
    if (current == AddressTable::kEmptySlot ||
        current == AddressTable::kDeletedSlot) {
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

bool AddressBitset::remove(uintptr_t addr) {
  if (addr == _k_empty_slot || addr == _k_deleted_slot) {
    return false; // Can't remove sentinel values
  }

  // Hash once for both chunk and slot lookup
  uint64_t hash;
  AddressTable *table = get_table(addr, hash);
  if (!table) {
    return false;
  }

  uint32_t slot = hash_to_slot(hash, table->table_mask);

  // Linear probing to find the address
  for (unsigned probe = 0; probe < AddressTable::kMaxProbeDistance; ++probe) {
    uintptr_t current = table->slots[slot].load(std::memory_order_acquire);

    if (current == AddressTable::kEmptySlot) {
      // Hit an empty slot - address not in table
      return false;
    }

    if (current == AddressTable::kDeletedSlot) {
      // Skip tombstones, continue probing
      slot = (slot + 1) & table->table_mask;
      continue;
    }

    if (current == addr) {
      // Found it - mark as deleted (tombstone)
      if (table->slots[slot].compare_exchange_strong(
              current, AddressTable::kDeletedSlot, std::memory_order_acq_rel)) {
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
    for (size_t chunk_idx = 0; chunk_idx < kMaxChunks; ++chunk_idx) {
      AddressTable *table =
          _chunk_tables[chunk_idx].load(std::memory_order_acquire);
      if (table) {
        for (unsigned i = 0; i < table->table_size; ++i) {
          table->slots[i].store(AddressTable::kEmptySlot,
                                std::memory_order_relaxed);
        }
        table->count.store(0, std::memory_order_relaxed);
      }
    }
  }
}

int AddressBitset::count() const {
  if (!_chunk_tables) {
    return 0;
  }

  int total = 0;
  for (size_t i = 0; i < kMaxChunks; ++i) {
    AddressTable *table = _chunk_tables[i].load(std::memory_order_relaxed);
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
  for (size_t i = 0; i < kMaxChunks; ++i) {
    if (_chunk_tables[i].load(std::memory_order_relaxed) != nullptr) {
      ++active;
    }
  }
  return active;
}

} // namespace ddprof
