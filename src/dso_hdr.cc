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

#include <algorithm>
#include <cassert>
#include <fcntl.h>
#include <numeric>
#include <unistd.h>

namespace ddprof {

using DsoFindRes = DsoHdr::DsoFindRes;
using DsoRange = DsoHdr::DsoRange;

namespace {
static FILE *procfs_map_open(int pid, const char *path_to_proc = "") {
  char buf[1024] = {0};
  auto n = snprintf(buf, 1024, "%s/proc/%d/maps", path_to_proc, pid);
  if (n >= 1024) { // unable to snprintf everything
    return nullptr;
  }
  return fopen(buf, "r");
}

#ifndef NDEBUG
static bool ip_in_procline(char *line, uint64_t ip) {
  static const char spec[] = "%lx-%lx %4c %lx %*x:%*x %*d%n";
  uint64_t m_start = 0;
  uint64_t m_end = 0;
  uint64_t m_off = 0;
  char m_mode[4] = {0};
  int m_p = 0;

  if (4 != sscanf(line, spec, &m_start, &m_end, m_mode, &m_off, &m_p)) {
    LG_WRN("Failed to scan mapfile line (search)");
    return false;
  }

  return ip >= m_start && ip <= m_end;
}

static void pid_find_ip(int pid, uint64_t ip,
                        const std::string &path_to_proc = "") {
  FILE *mpf = procfs_map_open(pid, path_to_proc.c_str());
  if (!mpf) {
    if (process_is_alive(pid))
      LG_DBG("Couldn't find ip:0x%lx for %d, process is dead", ip, pid);
    else
      LG_DBG("Couldn't find ip:0x%lx for %d, mysteriously", ip, pid);
    return;
  }
  defer { fclose(mpf); };

  char *buf = NULL;
  size_t sz_buf = 0;
  while (-1 != getline(&buf, &sz_buf, mpf)) {
    if (ip_in_procline(buf, ip)) {
      LG_DBG("[DSO] Found ip:0x%lx for %d", ip, pid);
      LG_DBG("[DSO] %.*s", (int)strlen(buf) - 1, buf);
      free(buf);
      return;
    }
  }
  free(buf);

  LG_DBG("[DSO] Couldn't find ip:0x%lx for %d", ip, pid);
  return;
}
#endif
} // namespace

/***************/
/*    STATS    */
/***************/

const char *DsoStats::s_event_dbg_str[] = {
    DSO_EVENT_TABLE(X_DSO_EVENT_DBG_STR)};

void DsoStats::log() const {
  for (int event_type = 0; event_type < kNbDsoEventTypes; ++event_type) {
    const auto &metric_vec = _metrics[event_type];
    for (int i = 0; i < dso::kNbDsoTypes; ++i) {
      if (metric_vec[i]) {
        const char *dso_type_str =
            dso::dso_type_str(static_cast<dso::DsoType>(i));
        LG_NTC("[DSO] %10s | %10s | %8lu |", s_event_dbg_str[event_type],
               dso_type_str, metric_vec[i]);
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
DsoHdr::DsoHdr() {
  // Test different places for existence of /proc
  // A given procfs can only work if its PID namespace is the same as mine.
  // Fortunately, `/proc/self` will return a symlink to my process ID in the
  // corresponding namespace, so this is easy to check
  char pid_str[sizeof("1073741824")] = {}; // Linux max pid/tid is 2^30
  if (-1 != readlink("/host/proc/self", pid_str, sizeof(pid_str)) &&
      getpid() == strtol(pid_str, NULL, 10)) {
    // @Datadog we often mount to /host the /proc files
    _path_to_proc = "/host";
  }
  // 0 element is error element
  _file_info_vector.emplace_back(FileInfo(), 0, true);
}

namespace {
bool string_readlink(const char *path, std::string &link_name) {
  char buff[1024];
  ssize_t len = ::readlink(path, buff, sizeof(buff) - 1);
  if (len != -1) {
    buff[len] = '\0';
    link_name = std::string(buff);
    return true;
  }
  return false;
}
} // namespace

bool DsoHdr::find_exe_name(pid_t pid, std::string &exe_name) {
  char exe_link[1024];
  sprintf(exe_link, "%s/proc/%d/exe", _path_to_proc.c_str(), pid);
  return string_readlink(exe_link, exe_name);
}

DsoFindRes DsoHdr::dso_find_first_std_executable(pid_t pid) {
  const DsoMap &map = _map[pid];
  DsoMapConstIt it = map.lower_bound(0);
  // look for the first executable standard region
  while (it != map.end() && !it->second._executable &&
         it->second._type != dso::kStandard) {
    ++it;
  }
  if (it == map.end()) {
    return find_res_not_found(map);
  }
  return std::make_pair<DsoMapConstIt, bool>(std::move(it), true);
}

DsoFindRes DsoHdr::dso_find_closest(const DsoMap &map, pid_t pid,
                                    ElfAddress_t addr) {
  bool is_within = false;
  // First element not less than (can match a start addr)
  DsoMapConstIt it = map.lower_bound(addr);
  if (it != map.end()) {
    is_within = it->second.is_within(pid, addr);
    if (is_within) { // exact match
      return std::make_pair<DsoMapConstIt, bool>(std::move(it),
                                                 std::move(is_within));
    }
  }
  // previous element is more likely to contain our addr
  if (it != map.begin()) {
    --it;
  } else { // map is empty
    return find_res_not_found(map);
  }
  is_within = it->second.is_within(pid, addr);
  return std::make_pair<DsoMapConstIt, bool>(std::move(it),
                                             std::move(is_within));
}

// Find the closest and indicate if we found a dso matching this address
DsoFindRes DsoHdr::dso_find_closest(pid_t pid, ElfAddress_t addr) {
  return dso_find_closest(_map[pid], pid, addr);
}

bool DsoHdr::dso_handled_type_read_dso(const Dso &dso) {
  if (dso._type != dso::kStandard) {
    // only handle standard path for now
    _stats.incr_metric(DsoStats::kUnhandledDso, dso._type);
    return false;
  }
  return true;
}

DsoRange DsoHdr::get_intersection(DsoMap &map, const Dso &dso) {
  if (map.empty()) {
    return std::make_pair<DsoMapIt, DsoMapIt>(map.end(), map.end());
  }
  // Get element after (with a start addr over the current)
  DsoMapIt first_el = map.lower_bound(dso._start);
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
  DsoMapIt start = map.end();
  DsoMapIt end = map.end();

  // Loop accross the possible range keeping track of first and last
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
  return std::make_pair<DsoMapIt, DsoMapIt>(std::move(start), std::move(end));
}

// erase range of elements
void DsoHdr::erase_range(DsoMap &map, const DsoRange &range) {
  // region maps are kept (as they are used for several pids)
  map.erase(range.first, range.second);
}

DsoFindRes DsoHdr::dso_find_adjust_same(DsoMap &map, const Dso &dso) {
  bool found_same = false;
  DsoMapIt it = map.find(dso._start);

  // comparator only looks at start ptr
  if (it != map.end()) {
    // if it is the same or smaller, we keep the current dso
    found_same = it->second.adjust_same(dso);
  }
  return std::make_pair<DsoFindRes::first_type, DsoFindRes::second_type>(
      std::move(it), std::move(found_same));
}

FileInfoId_t DsoHdr::get_or_insert_file_info(const Dso &dso) {
  if (dso._id != k_file_info_undef) {
    // already looked up this dso
    return dso._id;
  }
  _stats.incr_metric(DsoStats::kTargetDso, dso._type);
  return update_id_and_path(dso);
}

FileInfoId_t DsoHdr::update_id_and_path(const Dso &dso) {
  if (dso._type != dso::DsoType::kStandard) {
    dso._id = k_file_info_error; // no file associated
    return dso._id;
  }

  FileInfo file_info = find_file_info(dso);
  if (!file_info._inode) {
    dso._id = k_file_info_error;
    return dso._id;
  }

  // check if we already encountered binary
  FileInfoInodeKey key(file_info._inode, file_info._size);
  auto it = _file_info_inode_map.find(key);
  if (it == _file_info_inode_map.end()) {
    dso._id = _file_info_vector.size();
    _file_info_inode_map.emplace(std::move(key), dso._id);
#ifdef DEBUG
    LG_NTC("New file %d - %s - %ld", dso._id, file_info._path.c_str(),
           file_info._size);
#endif
    _file_info_vector.emplace_back(std::move(file_info), dso._id);
  } else { // already exists
    dso._id = it->second;
    // update with latest location
    if (file_info._path != _file_info_vector[dso._id]._info._path) {
      _file_info_vector[dso._id]._info = file_info;
      _file_info_vector[dso._id]._errored = false; // allow retry with new file
    }
  }
  return dso._id;
}

bool DsoHdr::erase_overlap(const Dso &dso) {
  DsoMap &map = _map[dso._pid];
  DsoRange range = get_intersection(map, dso);
  if (range.first != map.end()) {
    erase_range(map, range);
    return true;
  }
  return false;
}

DsoFindRes DsoHdr::insert_erase_overlap(DsoMap &map, Dso &&dso) {
  DsoFindRes find_res = dso_find_adjust_same(map, dso);
  // nothing to do if already exists
  if (find_res.second) {
    // TODO: should we erase overlaps here ?
    return find_res;
  }

  DsoRange range = get_intersection(map, dso);

  if (range.first != map.end()) {
    erase_range(map, range);
  }
  _stats.incr_metric(DsoStats::kNewDso, dso._type);
  LG_DBG("[DSO] : Insert %s", dso.to_string().c_str());
  // warning rvalue : do not use dso after this line
  return map.insert(std::make_pair<ProcessAddress_t, Dso>(
      ProcessAddress_t(dso._start), std::move(dso)));
}

DsoFindRes DsoHdr::insert_erase_overlap(Dso &&dso) {
  return insert_erase_overlap(_map[dso._pid], std::move(dso));
}

DsoFindRes DsoHdr::dso_find_or_backpopulate(pid_t pid, ElfAddress_t addr) {
  DsoMap &map = _map[pid];
  if (addr < 4095) {
    LG_DBG("[DSO] Skipping 0 page");
    return find_res_not_found(map);
  }
  int nb_elts_added = 0;

  if (_backpopulate_state_map.find(pid) == _backpopulate_state_map.end()) {
    // we never encountered this PID
    pid_backpopulate(map, pid, nb_elts_added);
  }

  DsoFindRes find_res = dso_find_closest(map, pid, addr);
  if (!find_res.second && !nb_elts_added) { // backpopulate
    LG_DBG("[DSO] Couldn't find DSO for [%d](0x%lx). backpopulate", pid, addr);
    if (pid_backpopulate(map, pid, nb_elts_added) && nb_elts_added) {
      find_res = dso_find_closest(map, pid, addr);
    }
#ifndef NDEBUG
    if (!find_res.second) { // debug info
      pid_find_ip(pid, addr, _path_to_proc);
    }
#endif
  }
  return find_res;
}

void DsoHdr::pid_free(int pid) {
  _map.erase(pid);
  _backpopulate_state_map.erase(pid);
}

bool DsoHdr::pid_backpopulate(pid_t pid, int &nb_elts_added) {
  return pid_backpopulate(_map[pid], pid, nb_elts_added);
}

// Return false if proc map is not available
// Return true proc map was found, use nb_elts_added for number of added
// elements
bool DsoHdr::pid_backpopulate(DsoMap &map, pid_t pid, int &nb_elts_added) {
  // Following line creates the state for pid if it does not exist
  BackpopulateState &bp_state = _backpopulate_state_map[pid];
  ++bp_state._nbUnfoundDsos;
  if (bp_state._perm != kAllowed) { // retry
    return false;
  }
  bp_state._perm = kForbidden;
  nb_elts_added = 0;
  LG_DBG("[DSO] Backpopulating PID %d", pid);
  FILE *mpf = procfs_map_open(pid, _path_to_proc.c_str());
  if (!mpf) {
    LG_DBG("[DSO] Failed to open procfs for %d", pid);
    if (!process_is_alive(pid))
      LG_DBG("[DSO] Process nonexistant");
    return false;
  }
  defer { fclose(mpf); };

  char *buf = NULL;
  size_t sz_buf = 0;

  while (-1 != getline(&buf, &sz_buf, mpf)) {
    Dso dso = dso_from_procline(pid, buf);
    if (dso._pid == -1) { // invalid dso
      continue;
    }
    if ((insert_erase_overlap(map, std::move(dso))).second) {
      ++nb_elts_added;
    }
  }
  free(buf);
  return true;
}

Dso DsoHdr::dso_from_procline(int pid, char *line) {
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
  static const char spec[] = "%lx-%lx %4c %lx %*x:%*x %*d%n";
  uint64_t m_start = 0;
  uint64_t m_end = 0;
  uint64_t m_off = 0;
  char m_mode[4] = {0};
  int m_p = 0;
  // Check for formatting errors
  if (4 != sscanf(line, spec, &m_start, &m_end, m_mode, &m_off, &m_p)) {
    LG_ERR("[DSO] Failed to scan mapfile line");
    throw DDException(DD_SEVERROR, DD_WHAT_DSO);
  }

  // Make sure the name index points to a valid char
  char *p = &line[m_p], *q;
  while (isspace(*p))
    p++;
  if ((q = strchr(p, '\n')))
    *q = '\0';

  // Should we store non exec dso ?
  return Dso(pid, m_start, m_end - 1, m_off, std::string(p), 'x' == m_mode[2]);
}

FileInfo DsoHdr::find_file_info(const Dso &dso) {
  int64_t size;
  inode_t inode;
  // to ensure we always retrieve the file in the context of our process, we go
  // through proc maps
  // Example : /proc/<pid>/root/usr/local/bin/exe_file
  //   or      /host/proc/<pid>/root/usr/local/bin/exe_file
  std::string proc_path = _path_to_proc + "/proc/" + std::to_string(dso._pid) +
      "/root" + dso._filename;
  bool file_found = get_file_inode(proc_path.c_str(), &inode, &size);
  if (file_found) {
    return FileInfo(proc_path, size, inode);
  }
  LG_DBG("[DSO] Unable to find path to %s", dso._filename.c_str());
  return FileInfo();
}

int DsoHdr::get_nb_dso() const {
  unsigned total_nb_elts = 0;
  std::for_each(_map.begin(), _map.end(), [&](DsoPidMap::value_type const &el) {
    total_nb_elts += el.second.size();
  });
  return total_nb_elts;
}

void DsoHdr::reset_backpopulate_state() {
  for (auto &el : _backpopulate_state_map) {
    if (el.second._nbUnfoundDsos >
        BackpopulateState::_k_nb_requests_between_backpopulates) {
      el.second = BackpopulateState();
    }
  }
}

} // namespace ddprof
