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
  auto n = snprintf(buf, 1024, "%s/proc/%d/maps", path_to_proc, pid);
  if (n >= 1024) { // unable to snprintf everything
    return nullptr;
  }
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
DsoHdr::DsoHdr() {
  // keep dso_id 0 as a reserved value
  // Test different places for existence of /proc
  if (check_file_type("/host/proc", S_IFDIR)) {
    // @Datadog we often mount to /host the /proc files
    _path_to_proc = "/host";
  }
  // 0 element is error element
  _file_info_vector.emplace_back(FileInfo(), 0, true);
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

  // check if we already encountered binary (cache by region with offset)
  FileInfoInodeKey key_inode(file_info._inode, dso._pgoff, file_info._size);
  FileInfoPathKey key_path(dso._filename, dso._pgoff, file_info._size);

  dso._id = _file_info_vector.size();

  // inode find, check with path + inode
  auto it_inode = _file_info_inode_map.find(key_inode);
  auto it_path = _file_info_path_map.find(key_path);
  bool found_file[2] = {false, false};
  // if either finds use value of find
  if (it_inode != _file_info_inode_map.end()) {
    // The inode found it, this one is saffer
    dso._id = it_inode->second;
    found_file[0] = true; // inode
  }
  if (!found_file[0] && it_path != _file_info_path_map.end()) {
    // the inode did not find it and the path has it.
    dso._id = it_path->second;
    found_file[1] = true; // path
  }
  // NO ONE found it, insert new element
  if (!found_file[0] && !found_file[1]) {
#ifdef DEBUG
    LG_NTC("New file %d - %s - %ld", dso._id, file_info._path.c_str(),
           file_info._size);
#endif
    _file_info_vector.emplace_back(std::move(file_info), dso._id);
  } else if (file_info._path != _file_info_vector[dso._id]._info._path) {
    // Someone found it, use fresh info
    _file_info_vector[dso._id]._info = file_info;
    _file_info_vector[dso._id]._errored = false; // allow retry with new file
  }
  // insert in maps that did not find it yet
  if (!found_file[0]) {
    _file_info_inode_map.emplace(std::move(key_inode), dso._id);
  }
  if (!found_file[1]) {
    _file_info_path_map.emplace(std::move(key_path), dso._id);
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

  DsoFindRes find_res = dso_find_closest(map, pid, addr);
  if (!find_res.second) { // backpopulate
    // Following line creates the state for pid if it does not exist
    BackpopulateState &bp_state = _backpopulate_state_map[pid];
    ++bp_state._nbUnfoundDsos;
    if (bp_state._perm == kAllowed) { // retry
      bp_state._perm = kForbidden;    // ... but only once
      LG_NTC("[DSO] Couldn't find DSO for [%d](0x%lx). backpopulate", pid,
             addr);
      int nb_elts_added = 0;
      if (pid_backpopulate(map, pid, nb_elts_added) && nb_elts_added) {
        find_res = dso_find_closest(map, pid, addr);
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
  const Dso &dso = find_res.first->second;
  if (!dso_handled_type_read_dso(dso)) {
    // We can not mmap if we do not have a file
    LG_DBG("[DSO] Read DSO : Unhandled DSO %s", dso.to_string().c_str());
    find_res.second = false;
    return find_res;
  }

  // Find the cached segment
  const RegionHolder *region = find_or_insert_region(dso);
  if (!region || !region->get_region()) {
    LG_DBG("[DSO] Unable to retrieve region from DSO %s (region=%p)",
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

  if (file_region_offset > region->get_sz()) {
    LG_NTC("[DSO] Attempt to read past the dso file");
    find_res.second = false;
    return find_res;
  }

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
  FileInfoId_t id = get_or_insert_file_info(dso);
  if (id <= k_file_info_error) {
    return nullptr;
  }
  const auto find_res = _region_map.find(id);
  if (find_res == _region_map.end()) {
    size_t reg_size = dso._end - dso._start + 1;
    if (static_cast<unsigned>(_file_info_vector[id].get_size()) < dso._pgoff) {
      reg_size = 0;
    } else {
      reg_size =
          std::min(reg_size, _file_info_vector[id].get_size() - dso._pgoff);
    }
    const auto insert_res =
        _region_map.emplace(id,
                            RegionHolder(_file_info_vector[id].get_path(),
                                         reg_size, dso._pgoff, dso._type));
    assert(insert_res.second); // insertion always successful
    LG_DBG("[DSO] Inserted region for DSO %s, at %p(%lx)",
           dso.to_string().c_str(), insert_res.first->second.get_region(),
           dso._end - dso._start + 1);
    return &insert_res.first->second;
  } else { // iterator contains a pair key / value
    return &find_res->second;
  }
}

void DsoHdr::pid_free(int pid) { _map.erase(pid); }

bool DsoHdr::pid_backpopulate(pid_t pid, int &nb_elts_added) {
  return pid_backpopulate(_map[pid], pid, nb_elts_added);
}

// Return false if proc map is not available
// Return true proc map was found, use nb_elts_added for number of added
// elements
bool DsoHdr::pid_backpopulate(DsoMap &map, pid_t pid, int &nb_elts_added) {
  nb_elts_added = 0;
  ProcFileHolder proc_file_holder(pid, _path_to_proc);
  LG_DBG("[DSO] Backpopulating PID %d", pid);
  FILE *mpf = proc_file_holder._mpf;
  if (!mpf) {
    LG_NTC("[DSO] Failed to open procfs for %d", pid);
    if (!process_is_alive(pid))
      LG_NTC("[DSO] Process nonexistant");
    return false;
  }

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
  // check if file exists locally
  int64_t size;
  inode_t inode;
  bool file_found = get_file_inode(dso._filename.c_str(), &inode, &size);

  if (file_found) {
    return FileInfo(dso._filename, size, inode);
  }
  // whole host :
  // Example : /proc/<pid>/root/usr/local/bin/exe_file
  //   or      /host/proc/<pid>/root/usr/local/bin/exe_file
  std::string proc_path = _path_to_proc + "/proc/" + std::to_string(dso._pid) +
      "/root" + dso._filename;
  file_found = get_file_inode(proc_path.c_str(), &inode, &size);
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

int DsoHdr::get_nb_mapped_dso() const { return _region_map.size(); }
} // namespace ddprof
