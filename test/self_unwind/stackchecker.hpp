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

// #define CONSIDER_OFFSET
#ifdef CONSIDER_OFFSET
struct DwflSymbolKey {
  explicit DwflSymbolKey(const ddprof::Symbol &symbol)
      : _demangle_name(symbol._demangle_name), _offset(symbol._offset) {}
  std::string _demangle_name;
  Offset_t _offset;
  bool operator==(const DwflSymbolKey &other) const {
    return (_demangle_name == other._demangle_name && _offset == other._offset);
  }
};
#else
// Only consider demangled name for now
struct DwflSymbolKey {
  explicit DwflSymbolKey(const ddprof::Symbol &symbol)
      : _demangle_name(symbol._demangle_name) {}
  std::string _demangle_name;

  bool operator==(const DwflSymbolKey &other) const {
    return (_demangle_name == other._demangle_name);
  }
};
#endif

} // namespace suw

namespace std {
#ifdef CONSIDER_OFFSET
template <> struct hash<suw::DwflSymbolKey> {
  std::size_t operator()(const suw::DwflSymbolKey &k) const {
    // Combine hashes of standard types
    std::size_t hash_val = suw::hash_combine(
        hash<std::string>()(k._demangle_name), hash<Offset_t>()(k._offset));
    return hash_val;
  }
};
#else
template <> struct hash<suw::DwflSymbolKey> {
  std::size_t operator()(const suw::DwflSymbolKey &k) const {
    return hash<std::string>()(k._demangle_name);
  }
};

#endif
} // namespace std

namespace suw {

using json = nlohmann::json;

using SymbolMap = std::unordered_map<suw::DwflSymbolKey, ddprof::Symbol>;

// Append ip info to a json file
void add_symbol(json &j, const ddprof::Symbol &symbol);

void write_json_file(std::string_view exe_name, const SymbolMap &map,
                     std::string_view data_directory = "");

int compare_to_ref(std::string_view exe_name, const SymbolMap &map,
                   std::string_view data_directory = "");

} // namespace suw