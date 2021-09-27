#pragma once

extern "C" {
#include "ddprof_defs.h"
}
#include <string>
#include <unordered_map>

#include "dso_type.hpp"
#include "hash_helper.hpp"

namespace ddprof {

struct RegionKey {
  RegionKey(const std::string &full_path, ElfAddress_t offset, std::size_t sz,
            dso::DsoType path_type)
      : _full_path(full_path), _offset(offset), _sz(sz), _type(path_type) {}
  bool operator==(const RegionKey &o) const;
  std::string _full_path;
  ElfAddress_t _offset;
  std::size_t _sz;
  dso::DsoType _type; // although it is a function of path, lets keep it
};

// mmaps the given regions
class RegionHolder {
public:
  RegionHolder();
  RegionHolder(const std::string &full_path, size_t sz, uint64_t pgoff,
               dso::DsoType path_type);
  ~RegionHolder();
  RegionHolder(RegionHolder &&other) : RegionHolder() { swap(*this, other); }

  RegionHolder &operator=(RegionHolder &&other) {
    swap(*this, other);
    return *this;
  }

  RegionHolder(const RegionHolder &other) = delete;            // avoid copy
  RegionHolder &operator=(const RegionHolder &other) = delete; // avoid copy
  void *get_region() const { return _region; }
  std::size_t get_sz() const { return _sz; }

private:
  static void swap(RegionHolder &first, RegionHolder &second) noexcept {
    std::swap(first._region, second._region);
    std::swap(first._sz, second._sz);
    std::swap(first._type, second._type);
  }

  void *_region;
  std::size_t _sz;
  dso::DsoType _type;
};

} // namespace ddprof
namespace std {
template <> struct hash<ddprof::RegionKey> {
  std::size_t operator()(const ddprof::RegionKey &k) const {
    // Combine hashes of standard types
    std::size_t hash_val = ddprof::hash_combine(
        hash<std::string>()(k._full_path), hash<ElfAddress_t>()(k._offset));
    hash_val = ddprof::hash_combine(hash_val, hash<size_t>()(k._sz));
    return hash_val;
  }
};

} // namespace std
namespace ddprof {
// Associate files to mmaped regions (unique ptr to avoid copies)
typedef std::unordered_map<RegionKey, RegionHolder> RegionMap;

} // namespace ddprof