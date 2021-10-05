#include "dso.hpp"

extern "C" {
#include "ddprof_defs.h"
#include "logger.h"
}

#include "string_format.hpp"

namespace ddprof {

static const std::string s_vdso_str = "[vdso]";
static const std::string s_vsyscall_str = "[vsyscall]";
static const std::string s_stack_str = "[stack]";
static const std::string s_heap_str = "[heap]";
// anon and empty are the same (one comes from perf, the other from proc maps)
static const std::string s_anon_str = "//anon";

// invalid element
Dso::Dso()
    : _pid(-1), _start(), _end(), _pgoff(), _filename(), _id(0),
      _type(dso::kUndef), _executable(false), _errored(true) {}

Dso::Dso(pid_t pid, ElfAddress_t start, ElfAddress_t end, ElfAddress_t pgoff,
         std::string &&filename, bool executable)
    : _pid(pid), _start(start), _end(end), _pgoff(pgoff), _filename(filename),
      _id(0), _type(dso::kStandard), _executable(executable), _errored(false) {
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
  } else if (_filename.empty() ||
             _filename.substr(0, s_anon_str.length()) == s_anon_str) {
    _type = dso::kAnon;
  } else if (_filename[0] == '[') {
    _type = dso::kUndef;
  }
}

std::string Dso::to_string() const {
  return string_format("PID[%d] %lx-%lx %lx (%s)(T-%s)(%c)(ID#%d)", _pid,
                       _start, _end, _pgoff, _filename.c_str(),
                       dso::dso_type_str(_type), _executable ? 'x' : '-', _id);
}

std::string Dso::format_filename() const {
  if (_type == dso::kStandard) {
    return _filename;
  } else {
    return dso::dso_type_str(_type);
  }
}

std::ostream &operator<<(std::ostream &os, const Dso &dso) {
  os << dso.to_string() << std::endl;
  return os;
}

bool Dso::operator<(const Dso &o) const {
  // In priority consider the pid in the order
  if (_pid < o._pid) {
    return true;
  } else if (_pid > o._pid) {
    return false;
  } else { // then look at the start address
    if (_start < o._start) {
      return true;
    }
  }
  return false;
}

// perf does not return the same sizes as proc maps
// Example :
// PID<1> 7f763019e000-7f76304ddfff (//anon)
// PID<1> 7f763019e000-7f76304de000 ()

bool Dso::same_or_smaller(const Dso &o) const {
  if (_start != o._start) {
    return false;
  }
  if (_pgoff != o._pgoff) {
    return false;
  }
  if (_end < o._end) {
    return false;
  }
  if (_type != o._type) {
    return false;
  }

  // only compare filename if we are backed by real files
  if (_type == dso::kStandard && _filename != o._filename) {
    return false;
  }
  if (_executable != o._executable) {
    return false;
  }

  return true;
}

bool Dso::intersects(const Dso &o) const {
  if (is_within(o._pid, o._start)) {
    return true;
  }
  if (is_within(o._pid, o._end)) {
    return true;
  }
  return false;
}

bool Dso::is_within(pid_t pid, ElfAddress_t addr) const {
  if (pid != _pid) {
    return false;
  }
  return (addr >= _start) && (addr <= _end);
}

} // namespace ddprof
