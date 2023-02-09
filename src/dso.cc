// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dso.hpp"

#include "constants.hpp"
#include "ddprof_defs.hpp"
#include "logger.hpp"
#include "string_format.hpp"

#include <string_view>

namespace ddprof {

static const std::string_view s_vdso_str = "[vdso]";
static const std::string_view s_vsyscall_str = "[vsyscall]";
static const std::string_view s_stack_str = "[stack]";
static const std::string_view s_heap_str = "[heap]";
// anon and empty are the same (one comes from perf, the other from proc maps)
static const std::string_view s_anon_str = "//anon";
static const std::string_view s_jsa_str = ".jsa";
static const std::string_view s_mem_fd_str = "/memfd";
// Example of these include : anon_inode:[perf_event]
static const std::string_view s_anon_inode_str = "anon_inode";
// dll should not be considered as elf files
static const std::string_view s_dll_str = ".dll";
// Example socket:[123456]
static const std::string_view s_socket_str = "socket";
// null elements
static const std::string_view s_dev_zero_str = "/dev/zero";
static const std::string_view s_dev_null_str = "/dev/null";
static const std::string_view s_dd_profiling_str = k_libdd_profiling_name;

// invalid element
Dso::Dso()
    : _pid(-1), _start(), _end(), _pgoff(), _filename(), _inode(),
      _type(dso::kUndef), _executable(false), _id(k_file_info_error) {}

Dso::Dso(pid_t pid, ElfAddress_t start, ElfAddress_t end, ElfAddress_t pgoff,
         std::string &&filename, bool executable, inode_t inode)
    : _pid(pid), _start(start), _end(end), _pgoff(pgoff), _filename(filename),
      _inode(inode), _type(dso::kStandard), _executable(executable),
      _id(k_file_info_undef) {
  // note that substr manages the case where len str < len vdso_str
  if (_filename.substr(0, s_vdso_str.length()) == s_vdso_str) {
    _type = dso::kVdso;
  } else if (_filename.substr(0, s_vsyscall_str.length()) == s_vsyscall_str) {
    _type = dso::kVsysCall;
  } else if (_filename.substr(0, s_stack_str.length()) == s_stack_str) {
    _type = dso::kStack;
  } else if (_filename.substr(0, s_heap_str.length()) == s_heap_str) {
    _type = dso::kHeap;
    // Safeguard against other types of files we would not handle
  } else if (_filename.empty() || _filename.starts_with(s_anon_str) ||
             _filename.starts_with(s_anon_inode_str) ||
             _filename.starts_with(s_dev_zero_str) ||
             _filename.starts_with(s_dev_null_str) ||
             _filename.starts_with(s_mem_fd_str)) {
    _type = dso::kAnon;
  } else if ( // ends with .jsa
      _filename.ends_with(s_jsa_str) || _filename.ends_with(s_dll_str)) {
    _type = dso::kRuntime;
  } else if (_filename.substr(0, s_socket_str.length()) == s_socket_str) {
    _type = dso::kSocket;
  } else if (_filename[0] == '[') {
    _type = dso::kUndef;
  } else if (is_jit_dump_str(_filename, pid)) {
    _type = dso::kJITDump;
  } else { // check if this standard dso matches our internal dd_profiling lib
    std::size_t pos = _filename.rfind('/');
    if (pos != std::string::npos &&
        _filename.substr(pos + 1, s_dd_profiling_str.length()) ==
            s_dd_profiling_str) {
      _type = dso::kDDProfiling;
    }
  }
}

bool Dso::is_jit_dump_str(std::string_view file_path, pid_t pid) {
  // test if we finish by .dump before creating a string
  if (file_path.ends_with(".dump")) {
    // llvm uses this format
    std::string jit_dump_str = string_format("jit-%d.dump", pid);
    // this is to remove a gcc warning
    if (jit_dump_str.size() >= PTRDIFF_MAX) {
      return false;
    }
    if (file_path.ends_with(jit_dump_str)) {
      return true;
    }
  }
  return false;
}

std::string Dso::to_string() const {
  return string_format("PID[%d] %lx-%lx %lx (%s)(T-%s)(%c)(ID#%d)", _pid,
                       _start, _end, _pgoff, _filename.c_str(),
                       dso::dso_type_str(_type), _executable ? 'x' : '-', _id);
}

std::string Dso::format_filename() const {
  if (dso::has_relevant_path(_type)) {
    return _filename;
  } else {
    return dso::dso_type_str(_type);
  }
}

std::ostream &operator<<(std::ostream &os, const Dso &dso) {
  os << dso.to_string() << std::endl;
  return os;
}

// perf does not return the same sizes as proc maps
// Example :
// PID<1> 7f763019e000-7f76304ddfff (//anon)
// PID<1> 7f763019e000-7f76304de000 ()

bool Dso::adjust_same(const Dso &o) {
  if (_start != o._start) {
    return false;
  }
  if (_pgoff != o._pgoff) {
    return false;
  }
  if (_type != o._type) {
    return false;
  }
  // only compare filename if we are backed by real files
  if (_type == dso::kStandard &&
      (_filename != o._filename || _inode != o._inode)) {
    return false;
  }
  if (_executable != o._executable) {
    return false;
  }
  _end = o._end;
  return true;
}

bool Dso::intersects(const Dso &o) const {
  // Check order of points
  // Test that we have lowest-start <-> lowest-end  ... highiest-start
  if (_start < o._start) {
    // this Dso comes first check then it ends before the other
    if (_end < o._start) {
      return false;
    }
  } else if (o._end < _start) {
    // dso comes after, check that other ends before our start
    return false;
  }
  return true;
}

bool Dso::is_within(ElfAddress_t addr) const {
  return (addr >= _start) && (addr <= _end);
}

} // namespace ddprof
