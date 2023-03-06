// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

struct StringTable {
  std::unordered_map<std::string, size_t> table;

  size_t insert(const std::string &str) {
    auto loc = table.find(str);
    size_t ret;
    if (loc == table.end()) {
      ret = table.size();
      table[str] = ret;
    } else {
      ret = loc->second;
    }
    return ret;
  }

  size_t insert(int n) {
    return insert(std::to_string(n));
  }

  void serialize(nlohmann::json &array) {
    std::map<size_t, const std::string *> aggr;
    for (const auto &elem: table)
      aggr[elem.second] = &elem.first;

    for (const auto &elem: aggr)
      array.emplace_back(*elem.second);
  }
};

struct ThreadFrame {
  const std::string _method;
  int _line;
};

bool operator<(const ThreadFrame &A, const ThreadFrame &B);

struct ThreadFrameTable {
  std::map<ThreadFrame, size_t> table;

  size_t insert(const ThreadFrame &frame) {
    auto loc = table.find(frame);
    size_t ret;
    if (loc == table.end()) {
      ret = table.size();
      table[frame] = ret;
    } else {
      ret = loc->second;
    }
    return ret;
  }

  void serialize(nlohmann::json &array, StringTable &stab) {
    std::map<size_t, const ThreadFrame*> aggr;
    for (const auto &elem: table)
      aggr[elem.second] = &elem.first;

    for (const auto &f : aggr) {
      array.push_back(nlohmann::json::array());
      array.back()[0] = stab.insert(f.second->_method);
      array.back()[1] = stab.insert(f.second->_line);
    }
  }
};
