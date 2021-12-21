// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_file_info.hpp"

namespace ddprof {

bool FileInfoInodeKey::operator==(const FileInfoInodeKey &o) const {
  // default comparison is c++20
  return _inode == o._inode && _offset == o._offset && _sz == o._sz;
}

bool FileInfoPathKey::operator==(const FileInfoPathKey &o) const {
  // default comparison is c++20
  return _path == o._path && _offset == o._offset && _sz == o._sz;
}

} // namespace ddprof
