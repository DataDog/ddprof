// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.

#pragma once

#include <unordered_map>
#include <memory>
#include <mutex>          // std::mutex, std::unique_lock
#include <shared_mutex>

constexpr int BITS_PER_LEVEL = 16;

class AddressNode {
public:
  AddressNode(int level) : _level(level) {}

  // Returns true if a new node was inserted, false if it already existed
  bool Insert(uintptr_t address) {
    auto index = (address >> _level) & ((1 << BITS_PER_LEVEL) - 1);
    std::unique_lock lock{_mutex};
    auto &child = _children[index];
    if (!child) {
      child = std::make_unique<AddressNode>(_level - BITS_PER_LEVEL);
    }
    lock.unlock();
    if (_level == 0) {
      return true;
    } else {
      return child->Insert(address);
    }
  }

  // Returns true if a node was erased, false if it did not exist
  bool Erase(uintptr_t address) {
    auto index = (address >> _level) & ((1 << BITS_PER_LEVEL) - 1);
    std::unique_lock lock{_mutex};
    auto it = _children.find(index);
    if (it == _children.end()) {
      return false;
    }
    if (_level == 0 || it->second->Erase(address)) {
      _children.erase(it);
      return true;
    }
    lock.unlock();
    return false;
  }

private:

  int _level;
  std::unordered_map<uintptr_t, std::unique_ptr<AddressNode>> _children;
  std::shared_mutex _mutex;

  friend class AddressTree;
};

class AddressTree {
public:
  AddressTree()
      : _root(sizeof(uintptr_t) * 8 - BITS_PER_LEVEL) {}

  // Insert an address into the tree. Returns true if the address was not already in the tree
  bool Insert(uintptr_t address) {
    return _root.Insert(address);
  }

  // Erase an address from the tree. Returns true if the address was in the tree
  bool Erase(uintptr_t address) {
    return _root.Erase(address);
  }

private:
  AddressNode _root;
};
