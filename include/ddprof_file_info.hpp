// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"
#include "ddprof_file_info-i.hpp"
#include "hash_helper.hpp"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ddprof {

/// Defines file uniqueness
/// Considering we can have relative paths within several containers, we check
/// inodes
struct FileInfoInodeKey {
  FileInfoInodeKey(inode_t inode, std::size_t sz) : _inode(inode), _sz(sz) {}
  bool operator==(const FileInfoInodeKey &o) const = default;
  // inode is used as a key (instead of path which can be the same for several
  // containers). TODO: inode could be the same across several filesystems (size
  // is there to mitigate)
  inode_t _inode;
  std::size_t _sz;
};

} // namespace ddprof

namespace std {
template <> struct hash<ddprof::FileInfoInodeKey> {
  std::size_t operator()(const ddprof::FileInfoInodeKey &k) const {
    std::size_t seed = 0;
    ddprof::hash_combine(seed, k._inode);
    ddprof::hash_combine(seed, k._sz);
    return seed;
  }
};

} // namespace std

namespace ddprof {
struct FileInfo {
  FileInfo() : _size(0), _inode(0) {}
  FileInfo(std::string path, int64_t size, inode_t inode)
      : _path(std::move(path)), _size(size), _inode(inode) {}
  // we update with latest location
  std::string _path;
  int64_t _size;
  inode_t _inode;
};

/// Keeps metadata on the file associated to a key
class FileInfoValue {
public:
  FileInfoValue(FileInfo &&info, FileInfoId_t id)
      : _info(std::move(info)), _id(id) {}

  FileInfoId_t get_id() const { return _id; }
  int64_t get_size() const { return _info._size; }
  const std::string &get_path() const { return _info._path; }

  bool errored() const { return _errored; }
  void set_errored() const { _errored = true; }
  void reset_errored() const { _errored = false; }

  const FileInfo &info() const { return _info; }

private:
  FileInfo _info;
  mutable bool _errored =
      false; // a flag to avoid trying to read in a loop bad files

  FileInfoId_t _id; // unique ID matching index in table
};

using FileInfoInodeMap = std::unordered_map<FileInfoInodeKey, FileInfoId_t>;
using FileInfoVector = std::vector<FileInfoValue>;

} // namespace ddprof
