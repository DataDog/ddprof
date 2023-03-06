// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "timeline/timeline.hpp"

/*=============================== StringTable ================================*/
size_t StringTable::insert(const std::string &str) {
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

size_t StringTable::insert(int n) {
  return insert(std::to_string(n));
}

void StringTable::serialize(nlohmann::json &array) {
  std::map<size_t, const std::string *> aggr;
  for (const auto &elem: table)
    aggr[elem.second] = &elem.first;

  for (const auto &elem: aggr)
    array.emplace_back(*elem.second);
}


/*=============================== ThreadFrame ================================*/
bool operator<(const ThreadFrame &A, const ThreadFrame &B) {
  if (A._method != B._method) return A._method < B._method;
  if (A._line != B._line) return A._line < B._line;
  return  false;
}


/*============================= ThreadFrameTable =============================*/
size_t ThreadFrameTable::insert(const ThreadFrame &frame) {
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

void ThreadFrameTable::serialize(nlohmann::json &array, StringTable &stab) {
  std::map<size_t, const ThreadFrame*> aggr;
  for (const auto &elem: table)
    aggr[elem.second] = &elem.first;

  for (const auto &f : aggr) {
    array.push_back(nlohmann::json::array());
    array.back()[0] = stab.insert(f.second->_method);
    array.back()[1] = stab.insert(f.second->_line);
  }
}
