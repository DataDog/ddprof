// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "ddprof_file_info.hpp"

#include <fcntl.h>
#include <unistd.h>
#include "ddres_exception.hpp"

#include "logger.hpp"

namespace ddprof {


bool FileInfoInodeKey::operator==(const FileInfoInodeKey &o) const {
  // default comparison is c++20
  return _inode == o._inode && _sz == o._sz;
}

FileInfoValue::FileInfoValue(FileInfo &&info, FileInfoId_t id, int fd)
    : _info(info), _id(id) {
  _fd = dup(fd);
  if (_fd < 0) { // acceptable value for dwfl
    LG_WRN("[FileInfo] failed to duplicate %s", info._path.c_str());
    _errored = true;
  }
}

FileInfoValue::FileInfoValue(FileInfo &&info, FileInfoId_t id)
    : _info(info),  _id(id) {
  _fd = open (info._path.c_str(), O_RDONLY);
  if (_fd < 0) { // acceptable value for dwfl
    LG_WRN("[FileInfo] failed to open %s", info._path.c_str());
    _errored = true;
  }
  else {
    LG_DBG("[FileInfo] Success opening %s, ", info._path.c_str());
  }
}

void FileInfoValue::copy_values(const FileInfoValue& other) {
  _info = other._info;
  _errored = other._errored;
  _id = other._id;
  _fd = other._fd;
}

FileInfoValue::~FileInfoValue() {
  if (this->_fd != -1)
      close(this->_fd);
}

FileInfoValue::FileInfoValue(const FileInfoValue& other) {
  copy_values(other);
  if (other._fd >= 0) {
    this->_fd = dup(other._fd);
  }
}

FileInfoValue& FileInfoValue::operator=(const FileInfoValue& other) {
    if (this != &other) {
        close(this->_fd);
        copy_values(other);
        if (other._fd >= 0) {
          this->_fd = dup(other._fd);
        }
    }
    return *this;
}

FileInfoValue::FileInfoValue(FileInfoValue&& other) {
    copy_values(other);
    other._fd = -1;
}

FileInfoValue& FileInfoValue::operator=(FileInfoValue&& other) {
    // move assignment operator
    close(this->_fd);
    copy_values(other);
    other._fd = -1;
    return *this;
}
} // namespace ddprof
