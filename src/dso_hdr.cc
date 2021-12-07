// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "dso_hdr.hpp"

extern "C" {
#include "ddprof_defs.h"
#include "logger.h"
#include "procutils.h"
#include "signal_helper.h"
}
#include "ddres.h"
#include <algorithm>
#include <cassert>
#include <numeric>

namespace ddprof {

using DsoFindRes = DsoHdr::DsoFindRes;
using DsoRange = DsoHdr::DsoRange;

namespace {
static FILE *procfs_map_open(int pid, const char *path_to_proc = "") {
  char buf[1024] = {0};
  snprintf(buf, 1024, "%s/proc/%d/maps", path_to_proc, pid);
  return fopen(buf, "r");
}

struct ProcFileHolder {
  explicit ProcFileHolder(int pid, const std::string &path_to_proc = "") {
    _mpf = procfs_map_open(pid, path_to_proc.c_str());
  }
  ~ProcFileHolder() {
    if (_mpf)
      fclose(_mpf);
  }
  FILE *_mpf;
};

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

#ifndef NDEBUG
static void pid_find_ip(int pid, uint64_t ip,
                        const std::string &path_to_proc = "") {
  ProcFileHolder file_holder(pid, path_to_proc);
  FILE *mpf = file_holder._mpf;
  if (!mpf) {
    if (process_is_alive(pid))
      LG_DBG("Couldn't find ip:0x%lx for %d, process is dead", ip, pid);
    else
      LG_DBG("Couldn't find ip:0x%lx for %d, mysteriously", ip, pid);
    return;
  }

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
                         0);
}

/**********/
/* DsoHdr */
/**********/
DsoHdr::DsoHdr() : _next_dso_id(0) {
  // Test different places for existence of /proc
  if (check_file_type("/host/proc", S_IFDIR)) {
    // In Datadog we often mount to /host
    _path_to_proc = "/host";
  }
}

DsoFindRes DsoHdr::dso_find_first_std_executable(pid_t pid) {
  const DsoSet &set = _map[pid];
  Dso temp_dso(pid, 0, 0);
  DsoSetConstIt it = set.lower_bound(temp_dso);
  // look for the first executable standard region
  while (it != set.end() && !it->_executable && it->_type != dso::kStandard &&
         it->_pid == pid) {
    ++it;
  }
  if (it == set.end() || it->_pid != pid) {
    return find_res_not_found(set);
  }
  return std::make_pair<DsoSetConstIt, bool>(std::move(it), true);
}

DsoFindRes DsoHdr::dso_find_closest(const DsoSet &set, pid_t pid,
                                    ElfAddress_t addr) {
  bool is_within = false;
  // Create a fake object to search (this is not needed in c++ 14)
  Dso temp_dso(pid, addr, addr);
  // First element not less than (can match a start addr)
  DsoSetConstIt it = set.lower_bound(temp_dso);
  if (it != set.end()) { // map is empty
    is_within = it->is_within(pid, addr);
    if (is_within) { // exact match
      return std::make_pair<DsoSetConstIt, bool>(std::move(it),
                                                 std::move(is_within));
    }
  }
  // previous element is more likely to contain our addr
  if (it != set.begin()) {
    --it;
  } else { // map is empty
    return find_res_not_found(set);
  }
  is_within = it->is_within(pid, addr);
  return std::make_pair<DsoSetConstIt, bool>(std::move(it),
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

DsoRange DsoHdr::get_intersection(const DsoSet &set, const Dso &dso) {
  if (set.empty()) {
    return std::make_pair<DsoSetIt, DsoSetIt>(set.end(), set.end());
  }
  // Get element after (with a start addr over the current)
  DsoSetIt first_el = set.lower_bound(dso);
  // Lower bound will return the first over our current element.
  //         <700--1050> <1100--1500> <1600--2200>
  // Elt to insert :  <1000-------------2000>
  // Go to previous as it could also overlap
  while (first_el != set.begin()) {
    --first_el;
    // Stop when :
    // - start of the list
    // - different pid (as everything is ordered by pid)
    // - end is before start
    if (first_el->_end < dso._start || first_el->_pid != dso._pid) {
      break;
    }
  }
  // init in case we don't find anything
  DsoSetIt start = set.end();
  DsoSetIt end = set.end();

  // Loop accross the possible range keeping track of first and last
  while (first_el != set.end()) {
    if (dso.intersects(*first_el)) {
      if (start == set.end()) {
        start = first_el;
      }
      end = first_el;
    }
    // if we are past the dso (both different pid and start past the end)
    if (dso < *first_el && first_el->_start > dso._end) {
      break;
    }
    ++first_el;
  }
  // push end element (as it should be after the last element)
  if (end != set.end()) {
    ++end;
  }
  return std::make_pair<DsoSetIt, DsoSetIt>(std::move(start), std::move(end));
}

// erase range of elements
void DsoHdr::erase_range(DsoSet &set, const DsoRange &range) {
  // region maps are kept (as they are used for several pids)
  set.erase(range.first, range.second);
}

DsoFindRes DsoHdr::dso_find_same_or_smaller(const DsoSet &set, const Dso &dso) {
  DsoFindRes res =
      std::make_pair<DsoFindRes::first_type, DsoFindRes::second_type>(
          set.find(dso), false);
  // comparator only looks at start ptr
  if (res.first != set.end()) {
    // if it is the same or smaller, we keep the current dso
    res.second = res.first->same_or_smaller(dso);
  }
  return res;
}

DsoUID_t DsoHdr::find_or_add_dso_uid(const Dso &dso) {
  if (dso._type != dso::DsoType::kStandard) {
    return ++_next_dso_id;
  }
  RegionKey key(get_binary_loc_info(dso).second, dso._pgoff,
                dso._end - dso._start + 1, dso._type);
  auto it = _dso_uid_map.find(key);
  if (it == _dso_uid_map.end()) {
    DsoUID_t current_uid = _next_dso_id++;
    _dso_uid_map.emplace(std::move(key), current_uid);
    return current_uid;
  } else {
    return it->second;
  }
}

DsoFindRes DsoHdr::insert_erase_overlap(DsoSet &set, Dso &&dso) {
  DsoFindRes find_res = dso_find_same_or_smaller(set, dso);
  // nothing to do if already exists
  if (find_res.second)
    return find_res;

  DsoRange range = get_intersection(set, dso);

  if (range.first != set.end()) {
    erase_range(set, range);
  }
  _stats.incr_metric(DsoStats::kNewDso, dso._type);

  // Warning : adding new inserts should update UIDs
  dso._id = find_or_add_dso_uid(dso);
  LG_DBG("[DSO] : Insert %s", dso.to_string().c_str());
  // warning rvalue : do not use dso after this line
  return set.insert(dso);
}

DsoFindRes DsoHdr::insert_erase_overlap(Dso &&dso) {
  return insert_erase_overlap(_map[dso._pid], std::move(dso));
}

DsoFindRes DsoHdr::dso_find_or_backpopulate(pid_t pid, ElfAddress_t addr) {
  DsoSet &set = _map[pid];
  if (addr < 4095) {
    LG_DBG("[DSO] Skipping 0 page");
    return find_res_not_found(set);
  }

  DsoFindRes find_res = dso_find_closest(set, pid, addr);
  if (!find_res.second) { // backpopulate
    // Following line creates the state for pid if it does not exist
    BackpopulateState &bp_state = _backpopulate_state_map[pid];
    ++bp_state._nbUnfoundDsos;
    if (bp_state._perm == kAllowed) { // retry
      bp_state._perm = kForbidden;    // ... but only once
      LG_NTC("[DSO] Couldn't find DSO for [%d](0x%lx). backpopulate", pid,
             addr);
      int nb_elts_added = 0;
      if (pid_backpopulate(set, pid, nb_elts_added) && nb_elts_added) {
        find_res = dso_find_closest(set, pid, addr);
      }
#ifndef NDEBUG
      if (!find_res.second) { // debug info
        pid_find_ip(pid, addr, _path_to_proc);
      }
#endif
    }
  }
  return find_res;
}

// addr : in the virtual mem of the pid specified
DsoFindRes DsoHdr::pid_read_dso(int pid, void *buf, size_t sz, uint64_t addr) {
  assert(buf);
  assert(sz > 0);

  DsoFindRes find_res = dso_find_or_backpopulate(pid, addr);
  if (!find_res.second) {
    return find_res;
  }
  const Dso &dso = *find_res.first;
  if (!dso_handled_type_read_dso(dso)) {
    // We can not mmap if we do not have a file
    LG_DBG("[DSO] Read DSO : Unhandled DSO %s", dso.to_string().c_str());
    find_res.second = false;
    return find_res;
  }

  // Find the cached segment
  const RegionHolder *region = find_or_insert_region(dso);
  if (!region || !region->get_region()) {
    LG_ERR("[DSO] Unable to retrieve region from DSO %s (region=%p)",
           dso._filename.c_str(), region);
    find_res.second = false;
    return find_res;
  }

  // Since addr is assumed in VM-space, convert it to segment-space, which is
  // file space minus the offset into the leading page of the segment
  if (addr < (dso._start) || (addr + sz) > (dso._end)) {
    LG_ERR("[DSO] Logic error when computing segment space.");
    find_res.second = false;
    return find_res;
  }

  Offset_t file_region_offset = (addr - dso._start);

  // At this point, we've
  //  Found a segment with matching parameters
  //  Adjusted addr to be a segment-offset
  //  Confirmed that the segment has the capacity to support our read
  // So let's read it!
  unsigned char *src = (unsigned char *)region->get_region();
  memcpy(buf, src + file_region_offset, sz);
  return find_res;
}

const RegionHolder *DsoHdr::find_or_insert_region(const Dso &dso) {
  const auto find_res = _region_map.find(dso._id);
  LG_DBG("[DSO] Get region - %s", dso.to_string().c_str());
  if (find_res == _region_map.end()) {
    // Assuming binaries are the same between containers is problematic.
    //    IMPORTANT TODO: Add container to the key
    std::string path_to_bin = dso._type == dso::kStandard
        ? get_binary_loc_info(dso).first
        : dso._filename;
    if (path_to_bin.empty()) {
      return nullptr; // file is gone, avoid insert (this can be a short lived
                      // process)
    }
    // Warning : there can still be a race condition here where file is deleted
    // between previous check and region mapping (at which point we will not
    // retry later if the same binary comes again)
    const auto insert_res =
        _region_map.emplace(dso._id,
                            RegionHolder(path_to_bin, dso._end - dso._start + 1,
                                         dso._pgoff, dso._type));
    assert(insert_res.second); // insertion should be successful
    return &insert_res.first->second;
  } else { // iterator contains a pair key / value
    return &find_res->second;
  }
}

void DsoHdr::pid_free(int pid) { _map.erase(pid); }

// Return false if proc map is not available
// Return true proc map was found, use nb_elts_added for number of added
// elements
bool DsoHdr::pid_backpopulate(DsoSet &set, pid_t pid, int &nb_elts_added) {
  nb_elts_added = 0;
  ProcFileHolder proc_file_holder(pid, _path_to_proc);
  LG_NTC("[DSO] Backpopulating PID %d", pid);
  FILE *mpf = proc_file_holder._mpf;
  if (!mpf) {
    LG_WRN("[DSO] Failed to open procfs for %d", pid);
    if (process_is_alive(pid))
      LG_WRN("[DSO] Process nonexistant");
    return false;
  }

  char *buf = NULL;
  size_t sz_buf = 0;

  while (-1 != getline(&buf, &sz_buf, mpf)) {
    Dso dso = dso_from_procline(pid, buf);
    if (dso._pid == -1) { // invalid dso
      continue;
    }
    if ((insert_erase_overlap(set, std::move(dso))).second) {
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

DsoHdr::FileLocInfo_t DsoHdr::get_binary_loc_info(const Dso &dso) {
  // check if file exists locally
  inode_t inode = get_file_inode(dso._filename.c_str());
  if (inode) {
    return std::make_pair<std::string, inode_t>(std::string(dso._filename),
                                                std::move(inode));
  }
  // whole host :
  // Example : /proc/<pid>/root/usr/local/bin/exe_file
  //   or      /host/proc/<pid>/root/usr/local/bin/exe_file
  std::string proc_path = _path_to_proc + "/proc/" + std::to_string(dso._pid) +
      "/root" + dso._filename;
  inode = get_file_inode(proc_path.c_str());
  if (inode) {
    return std::make_pair<std::string, inode_t>(std::move(proc_path),
                                                std::move(inode));
  }
  LG_DBG("[DSO] Unable to find path to %s", dso._filename.c_str());
  return std::make_pair<std::string, inode_t>(std::string(), 0);
}

int DsoHdr::get_nb_dso() const {
  unsigned total_nb_elts = 0;
  std::for_each(_map.begin(), _map.end(), [&](DsoPidMap::value_type const &el) {
    total_nb_elts += el.second.size();
  });
  return total_nb_elts;
}

int DsoHdr::get_nb_mapped_dso() const { return _region_map.size(); }
} // namespace ddprof
