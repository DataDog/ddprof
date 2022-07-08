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
  return _inode == o._inode && _offset == o._offset && _sz == o._sz;
}

static int safe_dup(int fd) {
    int copy = dup(fd); 
    if (copy < 0)
        LG_DBG("Warning copying invalid fd");
    return copy;
}

FileInfoValue::FileInfoValue(FileInfo &&info, FileInfoId_t id, bool errored)
    : _info(info), _errored(errored), _id(id) {
  _fd = open (info._path.c_str(), O_RDONLY);
  if (_fd < 0) { // acceptable value for dwfl
    LG_WRN("failed to open %s", info._path.c_str());
  }
  else {
    LG_WRN("Success opening %s, ", info._path.c_str());
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
        this->_fd = safe_dup(other._fd);
    }
    return *this;
}

FileInfoValue::FileInfoValue(FileInfoValue&& other) {
    // "move" file descriptor from OTHER to this: no duping is needed,
    // we just take the descriptor and set the one in OTHER to -1, so
    // it doesn't get closed by OTHER's destructor.
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
