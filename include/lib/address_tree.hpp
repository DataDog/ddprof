// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.

#pragma once

#include <atomic>
#include <memory>
#include <mutex> // std::mutex, std::unique_lock
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

namespace ddprof {

constexpr int BITS_PER_LEVEL = 16;

template <int BITS_PER_LEVEL, int LEVEL> class Node {
public:
  static constexpr int _level = LEVEL;

  bool insert(uintptr_t address) {
    auto index = (address >> _level) & ((1 << BITS_PER_LEVEL) - 1);
    std::unique_lock lock{_mutex};
    auto &value = _children[index];
    lock.unlock();
    return value.insert(address);
  }

  // we actually don't erase unless we are a leaf
  bool erase(uintptr_t address) {
    uint64_t index = (address >> _level) & ((1 << BITS_PER_LEVEL) - 1);
    std::unique_lock lock{_mutex};
    auto it = _children.find(index);
    if (it == _children.end()) {
      return false;
    }
    auto &val = it->second;
    lock.unlock();
    return val.erase(address);
  }

  void clear() {
    std::unique_lock lock{_mutex};
    for (auto &el : _children) {
      el.second.clear();
    }
  }

private:
  std::mutex _mutex;
  std::unordered_map<uintptr_t, Node<BITS_PER_LEVEL, LEVEL - BITS_PER_LEVEL>>
      _children;
};

template <int BITS_PER_LEVEL> class Node<BITS_PER_LEVEL, 0> {
public:
  static constexpr int _level = 0;

  bool insert(uintptr_t address) {
    auto index = address & ((1 << BITS_PER_LEVEL) - 1);
    std::unique_lock lock{_mutex};
    auto [_, inserted] = _addresses.insert(index);
    return inserted;
  }

  bool erase(uintptr_t address) {
    auto index = address & ((1 << BITS_PER_LEVEL) - 1);
    std::unique_lock lock{_mutex};
    return _addresses.erase(index) > 0;
  }

  bool empty() const {
    std::unique_lock lock{_mutex};
    return _addresses.empty();
  }

  void clear() {
    std::unique_lock lock{_mutex};
    _addresses.clear();
  }

private:
  std::mutex _mutex;
  std::unordered_set<uintptr_t> _addresses;
};

class AddressTree {
public:
  AddressTree() : _root() {}

  // insert an address into the tree. Returns true if the address was not
  // already in the tree
  bool insert(uintptr_t address) {
    _size.fetch_add(1, std::memory_order_relaxed);
    return _root.insert(address);
  }

  // erase an address from the tree. Returns true if the address was in the tree
  bool erase(uintptr_t address) {
    if (_root.erase(address)) {
      _size.fetch_add(-1, std::memory_order_relaxed);
    }
    return false;
  }

  void clear() {
    _size.exchange(0, std::memory_order_relaxed);
    _root.clear();
  }

  size_t size() { return _size.load(std::memory_order_relaxed); }

private:
  Node<BITS_PER_LEVEL, 64 - BITS_PER_LEVEL> _root;
  std::atomic<size_t> _size{};
  // todo we need to add some kind of global cleanup
};

} // namespace ddprof
