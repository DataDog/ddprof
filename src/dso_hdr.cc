// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dso_hdr.hpp"

#include "ddprof_defs.hpp"
#include "ddres.hpp"
#include "defer.hpp"
#include "logger.hpp"
#include "procutils.hpp"
#include "signal_helper.hpp"
#include "user_override.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <numeric>
#include <optional>
#include <unistd.h>

namespace ddprof {

namespace {

using FileHolder = std::unique_ptr<FILE, decltype([](FILE *f) { fclose(f); })>;

uint32_t mode_string_to_prot(const char mode[4]) {
  return ((mode[0] == 'r') ? PROT_READ : 0) |
      ((mode[1] == 'w') ? PROT_WRITE : 0) | ((mode[2] == 'x') ? PROT_EXEC : 0);
}

FileHolder open_proc_maps(int pid, const char *path_to_proc = "") {
  char proc_map_filename[PATH_MAX] = {};
  auto n = snprintf(proc_map_filename, std::size(proc_map_filename),
                    "%s/proc/%d/maps", path_to_proc, pid);
  if (n < 0 ||
      n >= static_cast<ssize_t>(
               std::size(proc_map_filename))) { // unable to snprintf everything
    return {};
  }

  FILE *f = fopen(proc_map_filename, "r");
  if (!f) {
    // Check if the file exists
    struct stat info;
    UIDInfo old_uids;
    if (stat(proc_map_filename, &info) == 0 &&
        // try to switch to file user
        IsDDResOK(user_override(info.st_uid, info.st_gid, &old_uids))) {
      f = fopen(proc_map_filename, "r");
      // switch back to initial user
      user_override(old_uids.uid, old_uids.gid);
    }
  }
  if (!f) {
    return {};
  }
  return FileHolder{f};
}
} // namespace

/***************/
/*    STATS    */
/***************/

const char *DsoStats::s_event_dbg_str[] = {
    DSO_EVENT_TABLE(X_DSO_EVENT_DBG_STR)};

void DsoStats::log() const {
  for (int event_type = 0; event_type < kNbDsoEventTypes; ++event_type) {
    const auto &metric_vec = _metrics[event_type];
    for (int i = 0; i < static_cast<int>(DsoType::kNbDsoTypes); ++i) {
      if (metric_vec[i]) {
        const char *dso_type = dso_type_str(static_cast<DsoType>(i));
        LG_NTC("[DSO] %10s | %10s | %8lu |", s_event_dbg_str[event_type],
               dso_type, metric_vec[i]);
      }
    }
  }
}

uint64_t DsoStats::sum_event_metric(DsoEventType dso_event) const {
  return std::accumulate(_metrics[dso_event].begin(), _metrics[dso_event].end(),
                         0UL);
}

/**********/
/* DsoHdr */
/**********/
DsoHdr::DsoHdr(std::string_view path_to_proc, int dd_profiling_fd)
    : _dd_profiling_fd(dd_profiling_fd) {
  if (path_to_proc.empty()) {
    // Test different places for existence of /proc
    // A given procfs can only work if its PID namespace is the same as mine.
    // Fortunately, `/proc/self` will return a symlink to my process ID in the
    // corresponding namespace, so this is easy to check
    char pid_str[sizeof("1073741824")] = {}; // Linux max pid/tid is 2^30
    if (-1 != readlink("/host/proc/self", pid_str, sizeof(pid_str)) &&
        getpid() ==
            strtol(pid_str, nullptr, 10)) { // NOLINT(readability-magic-numbers)
      // @Datadog we often mount to /host the /proc files
      _path_to_proc = "/host";
    }
  } else {
    _path_to_proc = path_to_proc;
  }
  // 0 element is error element
  _file_info_vector.emplace_back(FileInfo(), 0);
}

namespace {
bool string_readlink(const char *path, std::string &link_name) {
  char buff[PATH_MAX];
  ssize_t const len = ::readlink(path, buff, sizeof(buff) - 1);
  if (len != -1) {
    buff[len] = '\0';
    link_name = std::string(buff);
    return true;
  }
  return false;
}
} // namespace

bool DsoHdr::find_exe_name(pid_t pid, std::string &exe_name) {
  char exe_link[PATH_MAX];
  sprintf(exe_link, "%s/proc/%d/exe", _path_to_proc.c_str(), pid);
  return string_readlink(exe_link, exe_name);
}

DsoHdr::DsoFindRes DsoHdr::dso_find_first_std_executable(pid_t pid) {
  const DsoMap &map = _pid_map[pid]._map;
  auto it = map.lower_bound(0);
  // look for the first executable standard region
  while (it != map.end() && !it->second.is_executable() &&
         it->second._type != DsoType::kStandard) {
    ++it;
  }
  if (it == map.end()) {
    return find_res_not_found(map);
  }
  return {it, true};
}

DsoHdr::DsoFindRes DsoHdr::dso_find_closest(const DsoMap &map,
                                            ElfAddress_t addr) {
  bool is_within = false;
  // First element not less than (can match a start addr)
  auto it = map.lower_bound(addr);
  if (it != map.end()) {
    is_within = it->second.is_within(addr);
    if (is_within) { // exact match
      return {it, is_within};
    }
  }
  // previous element is more likely to contain our addr
  if (it != map.begin()) {
    --it;
  } else { // map is empty
    return find_res_not_found(map);
  }
  is_within = it->second.is_within(addr);
  return {it, is_within};
}

// Find the closest and indicate if we found a dso matching this address
DsoHdr::DsoFindRes DsoHdr::dso_find_closest(pid_t pid, ElfAddress_t addr) {
  return dso_find_closest(_pid_map[pid]._map, addr);
}

DsoHdr::DsoConstRange DsoHdr::get_elf_range(const DsoMap &map,
                                            DsoMapConstIt it) {
  const Dso &dso = it->second;
  auto not_same_file = [&dso](const DsoMap::value_type &x) {
    return dso._inode != x.second._inode || dso._filename != x.second._filename;
  };
  auto first =
      std::find_if(std::make_reverse_iterator(it), map.rend(), not_same_file);
  auto last = std::find_if(++it, map.end(), not_same_file);

  return {first.base(), last};
}

DsoHdr::DsoRange DsoHdr::get_intersection(DsoMap &map, const Dso &dso) {
  if (map.empty()) {
    return {map.end(), map.end()};
  }
  // Get element after (with a start addr over the current)
  auto first_el = map.lower_bound(dso._start);
  // Lower bound will return the first over our current element.
  //         <700--1050> <1100--1500> <1600--2200>
  // Elt to insert :  <1000-------------2000>
  // Go to previous as it could also overlap
  while (first_el != map.begin()) {
    --first_el;
    // Stop when :
    // - start of the list
    // - end is before start
    if (first_el->second._end < dso._start) {
      break;
    }
  }
  // init in case we don't find anything
  auto start = map.end();
  auto end = map.end();

  // Loop across the possible range keeping track of first and last
  while (first_el != map.end()) {
    if (dso.intersects(first_el->second)) {
      if (start == map.end()) {
        start = first_el;
      }
      end = first_el;
    }
    // if we are past the dso (both different pid and start past the end)
    if (first_el->second._start > dso._end) {
      break;
    }
    ++first_el;
  }
  // push end element (as it should be after the last element)
  if (end != map.end()) {
    ++end;
  }
  return {start, end};
}

// erase range of elements
void DsoHdr::erase_range(DsoMap &map, DsoRange range, const Dso &new_mapping) {
  if (range.first == range.second) {
    return;
  }
  auto last = std::prev(range.second);
  auto &last_mapping = last->second;
  // Truncate last mapping if files match or new mapping is anonymous:
  // a LOAD segment can have filesize < memsize as a size optimization when
  // the end of segment is all zeros (eg. bss). In that case elf loader /
  // dlopen will map the whole file + some extra space to cover the difference
  // between memsize and file size and then map an anonymous mapping over the
  // part not in the file.
  if (new_mapping.end() < last_mapping.end()) {

    if (new_mapping._start > last_mapping._start &&
        last_mapping.is_same_file(new_mapping)) {
      // New mapping fully inside the initial mapping:
      // split the initial mapping in two if mapped file is the same.
      Dso right_part{last_mapping};
      right_part.adjust_start(new_mapping.end() + 1);
      map[right_part.start()] = std::move(right_part);
      last_mapping.adjust_end(new_mapping._start - 1);
      --range.second;
    } else if (new_mapping._start <= last_mapping._start &&
               (last_mapping.is_same_file(new_mapping) ||
                last_mapping._type == DsoType::kAnon)) {
      // New mapping truncates the start of the initial mapping:
      // update start of initial mapping
      auto node_handle = map.extract(last);
      node_handle.mapped().adjust_start(new_mapping._end + 1);
      node_handle.key() = last_mapping._start;
      map.insert(std::move(node_handle));
      --range.second;
    }
  }

  if (range.first == range.second) {
    return;
  }

  auto &first_mapping = range.first->second;
  if (first_mapping.is_same_file(new_mapping) &&
      new_mapping._start > first_mapping._start) {
    // Truncate first mapping if files match:
    // elf loader / dlopen first map the whole file and then remap segments
    // inside the first mapping
    first_mapping.adjust_end(new_mapping._start - 1);
    ++range.first;
  }
  map.erase(range.first, range.second);
}

// cppcheck-suppress constParameterReference; cppcheck incorrectly warns that
// map could be a const reference
DsoHdr::DsoFindRes DsoHdr::dso_find_adjust_same(DsoMap &map, const Dso &dso) {
  bool found_same = false;
  auto const it = map.find(dso._start);

  // comparator only looks at start ptr
  if (it != map.end()) {
    // if it is the same or smaller, we keep the current dso
    found_same = it->second.adjust_same(dso);
  }
  return {it, found_same};
}

FileInfoId_t DsoHdr::get_or_insert_file_info(const Dso &dso) {
  if (dso._id != k_file_info_undef) {
    // already looked up this dso
    return dso._id;
  }
  _stats.incr_metric(DsoStats::kTargetDso, dso._type);
  return update_id_from_dso(dso);
}

FileInfoId_t DsoHdr::update_id_dd_profiling(const Dso &dso) {
  if (_dd_profiling_file_info != k_file_info_undef) {
    dso._id = _dd_profiling_file_info;
    return dso._id;
  }

  if (_dd_profiling_fd != -1) {
    // Path is not valid, don't use the map
    // fd already exists --> lookup directly
    dso._id = _file_info_vector.size();
    _dd_profiling_file_info = dso._id;
    _file_info_vector.emplace_back(FileInfo(dso._filename, 0, 0), dso._id);
    return _dd_profiling_file_info;
  }
  _dd_profiling_file_info = update_id_from_path(dso);
  return _dd_profiling_file_info;
}

FileInfoId_t DsoHdr::update_id_from_path(const Dso &dso) {

  FileInfo file_info = find_file_info(dso);
  if (!file_info._inode) {
    dso._id = k_file_info_error;
    return dso._id;
  }

  // check if we already encountered binary
  const FileInfoInodeKey key(file_info._inode, file_info._size);
  auto it = _file_info_inode_map.find(key);
  if (it == _file_info_inode_map.end()) {
    dso._id = _file_info_vector.size();
    _file_info_inode_map.emplace(key, dso._id);
    _file_info_vector.emplace_back(std::move(file_info), dso._id);
#ifdef DEBUG
    LG_NTC("New file %d - %s - %ld", dso._id, file_info._path.c_str(),
           file_info._size);
#endif
  } else { // already exists
    dso._id = it->second;
    // update with last location
    // looking up the actual path using mountinfo would prevent this
    if (file_info._path != _file_info_vector[dso._id]._info._path) {
      _file_info_vector[dso._id] = FileInfoValue(std::move(file_info), dso._id);
    }
  }
  return dso._id;
}

FileInfoId_t DsoHdr::update_id_from_dso(const Dso &dso) {
  if (!has_relevant_path(dso._type)) {
    dso._id = k_file_info_error; // no file associated
    return dso._id;
  }

  if (dso._type == DsoType::kDDProfiling) {
    return update_id_dd_profiling(dso);
  }

  return update_id_from_path(dso);
}

DsoHdr::DsoFindRes DsoHdr::insert_erase_overlap(PidMapping &pid_mapping,
                                                Dso &&dso) {
  DsoMap &map = pid_mapping._map;
  DsoFindRes find_res = dso_find_adjust_same(map, dso);
  // nothing to do if already exists
  if (find_res.second) {
    // TODO: should we erase overlaps here ?
    return find_res;
  }

  DsoRange const range = get_intersection(map, dso);

  if (range.first != map.end()) {
    erase_range(map, range, dso);
  }
  // JITDump Marker was detected for this PID
  if (dso._type == DsoType::kJITDump) {
    pid_mapping._jitdump_addr = dso._start;
  }
  _stats.incr_metric(DsoStats::kNewDso, dso._type);
  LG_DBG("[DSO] : Insert %s", dso.to_string().c_str());
  // warning rvalue : do not use dso after this line
  return map.insert({dso._start, std::move(dso)});
}

DsoHdr::DsoFindRes DsoHdr::insert_erase_overlap(Dso &&dso) {
  return insert_erase_overlap(_pid_map[dso._pid], std::move(dso));
}

DsoHdr::DsoFindRes DsoHdr::dso_find_or_backpopulate(PidMapping &pid_mapping,
                                                    pid_t pid,
                                                    ElfAddress_t addr) {
  constexpr uint64_t k_zero_page_limit = 4096;
  if (addr < k_zero_page_limit) {
    LG_DBG("[DSO] Skipping 0 page");
    return find_res_not_found(pid_mapping._map);
  }

  DsoFindRes find_res = dso_find_closest(pid_mapping._map, addr);
  if (!find_res.second) { // backpopulate
    LG_DBG("[DSO] Couldn't find DSO for [%d](0x%lx). backpopulate", pid, addr);
    int nb_elts_added = 0;
    if (pid_backpopulate(pid_mapping, pid, nb_elts_added) && nb_elts_added) {
      find_res = dso_find_closest(pid_mapping._map, addr);
    }
  }
  return find_res;
}

DsoHdr::DsoFindRes DsoHdr::dso_find_or_backpopulate(pid_t pid,
                                                    ElfAddress_t addr) {
  PidMapping &pid_mapping = _pid_map[pid];
  return dso_find_or_backpopulate(pid_mapping, pid, addr);
}

void DsoHdr::pid_free(int pid) {
  _pid_map.erase(pid);
  _backpopulate_state_map.erase(pid);
}

bool DsoHdr::pid_backpopulate(pid_t pid, int &nb_elts_added) {
  return pid_backpopulate(_pid_map[pid], pid, nb_elts_added);
}

// Return false if proc map is not available
// Return true proc map was found, use nb_elts_added for number of added
// elements
bool DsoHdr::pid_backpopulate(PidMapping &pid_mapping, pid_t pid,
                              int &nb_elts_added) {
  // Following line creates the state for pid if it does not exist
  BackpopulateState &bp_state = _backpopulate_state_map[pid];
  ++bp_state._nbUnfoundDsos;
  if (bp_state._perm != kAllowed) { // retry
    return false;
  }
  nb_elts_added = 0;
  LG_DBG("[DSO] Backpopulating PID %d", pid);
  auto proc_map_file_holder = open_proc_maps(pid, _path_to_proc.c_str());
  if (!proc_map_file_holder) {
    LG_DBG("[DSO] Failed to open procfs for %d", pid);
    if (!process_is_alive(pid)) {
      LG_DBG("[DSO] Process nonexistant");
    }
    return false;
  }
  char *buf = nullptr;
  defer { free(buf); };
  size_t sz_buf = 0;

  while (-1 != getline(&buf, &sz_buf, proc_map_file_holder.get())) {
    Dso dso = dso_from_proc_line(pid, buf);
    if (dso._pid == -1) { // invalid dso
      continue;
    }
    if ((insert_erase_overlap(pid_mapping, std::move(dso))).second) {
      ++nb_elts_added;
    }
  }
  if (!nb_elts_added) {
    bp_state._perm = kForbidden;
  }
  return true;
}

Dso DsoHdr::dso_from_proc_line(int pid, const char *line) {
  // clang-format off
  // Example of format
  /*
    55d78839f000-55d7883a1000 r--p 00000000 fe:01 3287864                    /usr/local/bin/BadBoggleSolver_run
    55d7883a1000-55d7883a5000 r-xp 00002000 fe:01 3287864                    /usr/local/bin/BadBoggleSolver_run
    ...
    55d78a12b000-55d78a165000 rw-p 00000000 00:00 0                          [heap]
    ...
    7f531437b000-7f531439e000 r-xp 00001000 fe:01 3932979                    /usr/lib/x86_64-linux-gnu/ld-2.31.so
    7f531439e000-7f53143a6000 r--p 00024000 fe:01 3932979                    /usr/lib/x86_64-linux-gnu/ld-2.31.so
    7f53143a8000-7f53143a9000 rw-p 0002d000 fe:01 3932979                    /usr/lib/x86_64-linux-gnu/ld-2.31.so
    7f53143a9000-7f53143aa000 rw-p 00000000 00:00 0
    7ffcd6c68000-7ffcd6c89000 rw-p 00000000 00:00 0                          [stack]
    7ffcd6ce2000-7ffcd6ce6000 r--p 00000000 00:00 0                          [vvar]
    7ffcd6ce6000-7ffcd6ce8000 r-xp 00000000 00:00 0                          [vdso]
    ffffffffff600000-ffffffffff601000 r-xp 00000000 00:00 0                  [vsyscall]
  */
  // clang-format on
  uint64_t m_start = 0;
  uint64_t m_end = 0;
  uint64_t m_off = 0;
  char m_mode[4] = {0};
  int m_p = 0;
  uint32_t m_dev_major = 0;
  uint32_t m_dev_minor = 0;
  uint64_t m_inode = 0;

  // %n specifier does not increase count returned by sscanf
  constexpr int k_expected_number_of_matches = 7;
  // Check for formatting errors
  if (k_expected_number_of_matches !=
      // NOLINTNEXTLINE(cert-err34-c)
      sscanf(line, "%lx-%lx %4c %lx %x:%x %lu%n", &m_start, &m_end, m_mode,
             &m_off, &m_dev_major, &m_dev_minor, &m_inode, &m_p)) {
    LG_ERR("[DSO] Failed to scan proc line: %s", line);
    return {};
  }

  // trim spaces on the left
  std::string_view remaining{line + m_p};
  remaining.remove_prefix(
      +std::min(remaining.find_first_not_of(" \t"), remaining.size()));
  // remove new line at end if present
  if (remaining.ends_with('\n')) {
    remaining.remove_suffix(1);
  }

  return {pid,
          m_start,
          m_end - 1,
          m_off,
          std::string(remaining),
          m_inode,
          mode_string_to_prot(m_mode),
          DsoOrigin::kProcMaps};
}

FileInfo DsoHdr::find_file_info(const Dso &dso) {
  int64_t size;
  inode_t inode;
  // to ensure we always retrieve the file in the context of our process, we go
  // through proc maps
  // Example : /proc/<pid>/root/usr/local/bin/exe_file
  //   or      /host/proc/<pid>/root/usr/local/bin/exe_file
  std::string const proc_path = _path_to_proc + "/proc/" +
      std::to_string(dso._pid) + "/root" + dso._filename;
  if (get_file_inode(proc_path.c_str(), &inode, &size) && inode == dso._inode) {
    return {proc_path, size, inode};
  }
  // Try to find file in profiler mount namespace
  if (get_file_inode(dso._filename.c_str(), &inode, &size) &&
      inode == dso._inode) {
    return {dso._filename, size, inode};
  }

  LG_DBG("[DSO] Unable to find path to %s", dso._filename.c_str());
  return {};
}

int DsoHdr::get_nb_dso() const {
  unsigned total_nb_elts = 0;
  std::for_each(_pid_map.begin(), _pid_map.end(),
                [&](DsoPidMap::value_type const &el) {
                  total_nb_elts += el.second._map.size();
                });
  return total_nb_elts;
}

void DsoHdr::reset_backpopulate_state(int reset_threshold) {
  for (auto &el : _backpopulate_state_map) {
    if (el.second._nbUnfoundDsos >= reset_threshold) {
      el.second = BackpopulateState();
    }
  }
}

void DsoHdr::pid_fork(pid_t pid, pid_t parent_pid) {
  auto pid_mapping_it = _pid_map.find(parent_pid);
  if (pid_mapping_it == _pid_map.end()) {
    return;
  }
  auto &new_pid_mapping = _pid_map[pid];
  for (const auto &mapping : pid_mapping_it->second._map) {
    new_pid_mapping._map[mapping.first] = Dso{mapping.second, pid};
  }
}

} // namespace ddprof
