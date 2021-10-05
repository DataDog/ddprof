#pragma once

extern "C" {
#include "dwfl_internals.h"
#include <sys/types.h>
}
#include "ddres.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>

struct DwflWrapper {

  explicit DwflWrapper() : _dwfl(nullptr), _attached(false) {
    static const Dwfl_Callbacks proc_callbacks = {
        .find_elf = dwfl_linux_proc_find_elf,
        .find_debuginfo = dwfl_standard_find_debuginfo,
        .section_address = nullptr,
        .debuginfo_path = nullptr, // specify dir to look into
    };
    _dwfl = dwfl_begin(&proc_callbacks);
    if (!_dwfl) {
      LG_WRN("dwfl_begin was zero (%s)", dwfl_errmsg(-1));
      throw ddprof::DDException(ddres_error(DD_WHAT_DWFL_LIB_ERROR));
    }
  }

  DwflWrapper(DwflWrapper &&other) : _dwfl(nullptr), _attached(false) {
    swap(*this, other);
  }

  DwflWrapper &operator=(DwflWrapper &&other) {
    swap(*this, other);
    return *this;
  }

  DwflWrapper(const DwflWrapper &other) = delete;            // avoid copy
  DwflWrapper &operator=(const DwflWrapper &other) = delete; // avoid copy

  DDRes attach(pid_t pid, const Dwfl_Thread_Callbacks *callbacks,
               struct UnwindState *us);

  ~DwflWrapper() { dwfl_end(_dwfl); }

  static void swap(DwflWrapper &first, DwflWrapper &second) noexcept {
    std::swap(first._dwfl, second._dwfl);
    std::swap(first._attached, second._attached);
  }

  Dwfl *_dwfl;
  bool _attached;
};

class DwflHdr {
public:
  DwflWrapper &get_or_insert(pid_t pid);
  void clear_unvisited();
  void clear_pid(pid_t pid);

private:
  std::unordered_map<pid_t, DwflWrapper> _dwfl_map;
  std::unordered_set<pid_t> _visited_pid;
};
