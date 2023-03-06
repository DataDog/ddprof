// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <string>
#include <unordered_map>
#include <map>

#include <nlohmann/json.hpp>


/*=============================== StringTable ================================*/
struct StringTable {
  std::unordered_map<std::string, size_t> table;

  // Functions
  size_t insert(const std::string &str);
  size_t insert(int n);
  void serialize(nlohmann::json &array);
};


/*=============================== ThreadFrame ================================*/
struct ThreadFrame {
  const std::string _method;
  int _line;
};
bool operator<(const ThreadFrame &A, const ThreadFrame &B);


/*============================= ThreadFrameTable =============================*/
struct ThreadFrameTable {
  std::map<ThreadFrame, size_t> table;

  // Functions
  size_t insert(const ThreadFrame &frame);
  void serialize(nlohmann::json &array, StringTable &stab);
};

