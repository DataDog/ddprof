// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

extern "C" {
#include "ddprof_defs.h"
}
#include <string>
#include <unordered_map>
#include <vector>

#include "ddprof_file_info-i.hpp"
#include "hash_helper.hpp"

namespace ddprof {

/// Defines file uniqueness
/// Considering we can have relative paths within several containers, we check
/// inodes
struct FileInfoInodeKey {
  FileInfoInodeKey(inode_t inode, ElfAddress_t offset, std::size_t sz)
      : _inode(inode), _offset(offset), _sz(sz) {}
  bool operator==(const FileInfoInodeKey &o) const;
  // inode is used as a key (instead of path which can be the same for several
  // containers). TODO: inode could be the same across several filesystems (size
  // is there to mitigate)
  inode_t _inode;
  Offset_t _offset;
  std::size_t _sz;
};

} // namespace ddprof

namespace std {
template <> struct hash<ddprof::FileInfoInodeKey> {
  std::size_t operator()(const ddprof::FileInfoInodeKey &k) const {
    std::size_t hash_val = ddprof::hash_combine(hash<inode_t>()(k._inode),
                                                hash<Offset_t>()(k._offset));
    hash_val = ddprof::hash_combine(hash_val, hash<size_t>()(k._sz));
    return hash_val;
  }
};

} // namespace std

namespace ddprof {
struct FileInfo {
  FileInfo() : _size(0), _inode(0) {}
  FileInfo(const std::string &path, int64_t size, inode_t inode)
      : _path(path), _size(size), _inode(inode) {}
  // we update with latest location
  std::string _path;
  int64_t _size;
  inode_t _inode;
};

/// Keeps metadata on the file associated to a key
class FileInfoValue {
public:
  FileInfoValue(FileInfo &&info, FileInfoId_t id, bool errored = false)
      : _info(info), _errored(errored), _id(id) {}
  FileInfoId_t get_id() const { return _id; }
  int64_t get_size() const { return _info._size; }
  const std::string &get_path() const { return _info._path; }

  FileInfo _info;

  mutable bool _errored; // a flag to avoid trying to read in a loop bad files
private:
  FileInfoId_t _id; // unique ID matching index in table
};

typedef std::unordered_map<FileInfoInodeKey, FileInfoId_t> FileInfoInodeMap;
typedef std::vector<FileInfoValue> FileInfoVector;

} // namespace ddprof
