// Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

#pragma once

#include "symbol_table.hpp"
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <unordered_map>

namespace suw {

// Avoid flaky CI failures
static const int k_failure_threshold = 45;

static inline std::size_t hash_combine(std::size_t lhs, std::size_t rhs) {
  return rhs + 0x9e3779b9 + (lhs << 6) + (lhs >> 2);
}

// Only consider demangled name for now
struct DwflSymbolKey {
  explicit DwflSymbolKey(const ddprof::Symbol &symbol)
      : _demangle_name(symbol._demangle_name) {}
  std::string _demangle_name;

  bool operator==(const DwflSymbolKey &other) const {
    return (_demangle_name == other._demangle_name);
  }
};

} // namespace suw

namespace std {
template <> struct hash<suw::DwflSymbolKey> {
  std::size_t operator()(const suw::DwflSymbolKey &k) const {
    return hash<std::string>()(k._demangle_name);
  }
};

} // namespace std

namespace suw {

using json = nlohmann::json;

using SymbolMap = std::unordered_map<suw::DwflSymbolKey, ddprof::Symbol>;

// Append ip info to a json file
void add_symbol(json &j, const ddprof::Symbol &symbol);

void write_json_file(std::string exe_name, const SymbolMap &map,
                     std::string data_directory = "");

int compare_to_ref(std::string exe_name, const SymbolMap &map,
                   std::string data_directory = "");

} // namespace suw