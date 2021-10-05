#include "dso_hdr.hpp"

extern "C" {
#include "ddprof_defs.h"
#include "logger.h"
#include "signal_helper.h"
}
#include "ddres.h"
#include <cassert>
#include <numeric>

static FILE *procfs_map_open(int pid) {
  char buf[1024] = {0};
  snprintf(buf, 1024, "/proc/%d/maps", pid);
  return fopen(buf, "r");
}

namespace {
struct ProcFileHolder {
  explicit ProcFileHolder(int pid) { _mpf = procfs_map_open(pid); }
  ~ProcFileHolder() {
    if (_mpf)
      fclose(_mpf);
  }
  FILE *_mpf;
};
} // namespace

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
static void pid_find_ip(int pid, uint64_t ip) {
  ProcFileHolder file_holder(pid);
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
  if (buf != NULL) {
    free(buf);
  }

  LG_DBG("[DSO] Couldn't find ip:0x%lx for %d", ip, pid);
  return;
}
#endif

namespace ddprof {

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

} // namespace ddprof

using ddprof::Dso;
using ddprof::DsoRange;
using ddprof::DsoSetConstIt;
using ddprof::DsoSetIt;
using ddprof::DsoStats;

/**********/
/* DsoHdr */
/**********/

// Find the closest and indicate if we found a dso matching this address
DsoFindRes DsoHdr::dso_find_closest(pid_t pid, ElfAddress_t addr) {
  bool is_within = false;
  // Create a fake object to search (this is not needed in c++ 14)
  Dso temp_dso(pid, addr, addr);
  // First element not less than (can match a start addr)
  DsoSetConstIt it = _set.lower_bound(temp_dso);
  if (it == _set.end()) {
    return std::make_pair<ddprof::DsoSetConstIt, bool>(std::move(it),
                                                       std::move(is_within));
  }
  is_within = it->is_within(pid, addr);
  // go back one element to check if we find it
  if (!is_within && it != _set.begin()) {
    --it;
    is_within = it->is_within(pid, addr);
  }
  return std::make_pair<ddprof::DsoSetConstIt, bool>(std::move(it),
                                                     std::move(is_within));
}

bool DsoHdr::dso_handled_type_read_dso(const Dso &dso) {
  if (dso._type != ddprof::dso::kStandard) {
    // only handle standard path for now
    _stats.incr_metric(DsoStats::kUnhandledDso, dso._type);
    return false;
  }
  return true;
}

DsoRange DsoHdr::get_intersection(const Dso &dso) {
  if (_set.empty()) {
    return std::make_pair<DsoSetIt, DsoSetIt>(_set.end(), _set.end());
  }
  // Get element after (with a start addr over the current)
  DsoSetIt first_el = _set.lower_bound(dso);
  // Lower bound will return the first over our current element.
  //         <700--1050> <1100--1500> <1600--2200>
  // Elt to insert :  <1000-------------2000>
  // Go to previous as it could also overlap
  while (first_el != _set.begin()) {
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
  DsoSetIt start = _set.end();
  DsoSetIt end = _set.end();

  // Loop accross the possible range keeping track of first and last
  while (first_el != _set.end()) {
    if (dso.intersects(*first_el)) {
      if (start == _set.end()) {
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
  if (end != _set.end()) {
    ++end;
  }
  return std::make_pair<DsoSetIt, DsoSetIt>(std::move(start), std::move(end));
}

// get all elements of a pid
DsoRange DsoHdr::get_pid_range(pid_t pid) {
  Dso temp_dso(pid + 1, 0, 0);
  auto it_end = _set.lower_bound(Dso(pid + 1, 0, 0));
  auto it_start = _set.lower_bound(Dso(pid, 0, 0));
  return std::make_pair<DsoSetIt, DsoSetIt>(std::move(it_start),
                                            std::move(it_end));
}

// erase range of elements
void DsoHdr::erase_range(const DsoRange &range) {
  auto it = range.first;
  while (it != range.second) {
    // someone else could need this region, but we do not hold a reference
    // count (over optim ?)
    LG_DBG("[DSO] : Erase %s", it->to_string().c_str());
    ddprof::RegionKey key(it->_filename, it->_pgoff, it->_end - it->_start + 1,
                          it->_type);
    _region_map.erase(key);
    ++it;
  }

  _set.erase(range.first, range.second);
}

DsoFindRes DsoHdr::dso_find_same_or_smaller(const ddprof::Dso &dso) {
  DsoFindRes res =
      std::make_pair<DsoFindRes::first_type, DsoFindRes::second_type>(
          _set.find(dso), false);
  // comparator only looks at start ptr
  if (res.first != _set.end()) {
    // if it is the same or smaller, we keep the current dso
    res.second = res.first->same_or_smaller(dso);
  }
  return res;
}

DsoFindRes DsoHdr::insert_erase_overlap(ddprof::Dso &&dso) {
  DsoFindRes find_res = dso_find_same_or_smaller(dso);
  // nothing to do if already exists
  if (find_res.second)
    return find_res;

  // todo : optimise this by changing to a map for cases where a single el can
  // be replaced
  DsoRange range = get_intersection(dso);

  if (range.first != _set.end()) {
    erase_range(range);
  }
  _stats.incr_metric(DsoStats::kNewDso, dso._type);
  // Warning : adding new inserts should update UIDs
  dso._id = _next_dso_id++;
  LG_DBG("[DSO] : Insert %s", dso.to_string().c_str());
  // warning rvalue : do not use dso after this line
  return _set.insert(dso);
}

DsoFindRes DsoHdr::dso_find_or_backpopulate(pid_t pid, ElfAddress_t addr) {
  DsoFindRes find_res = dso_find_closest(pid, addr);
  if (!find_res.second) { // backpopulate
    // Following line creates the state for pid if it does not exist
    BackpopulateState &bp_state = _backpopulate_state_map[pid];
    ++bp_state._nbUnfoundDsos;
    if (bp_state._perm == kAllowed) { // retry
      bp_state._perm = kForbidden;    // ... but only once
      LG_NTC("[DSO] Couldn't find DSO for [%d](0x%lx). backpopulate", pid,
             addr);
      if (pid_backpopulate(pid)) {
        find_res = dso_find_closest(pid, addr);
      }
#ifndef NDEBUG
      if (!find_res.second) { // debug info
        pid_find_ip(pid, addr);
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
  DsoFindRes find_res = std::make_pair<DsoSetIt, bool>(_set.end(), false);

  // Can skip the zero page
  if (addr < 4095) {
    LG_DBG("[DSO] Skipping 0 page");
    return find_res;
  }

  find_res = dso_find_or_backpopulate(pid, addr);
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
  const ddprof::RegionHolder &region = find_or_insert_region(dso);
  if (!region.get_region()) {
    LG_ERR("[DSO] Unable to retrieve region from DSO.");
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

  Offset_t file_region_offset = (addr - dso._start) + dso._pgoff;

  // At this point, we've
  //  Found a segment with matching parameters
  //  Adjusted addr to be a segment-offset
  //  Confirmed that the segment has the capacity to support our read
  // So let's read it!
  unsigned char *src = (unsigned char *)region.get_region();
  memcpy(buf, src + file_region_offset, sz);
  return find_res;
}

const ddprof::RegionHolder &
DsoHdr::find_or_insert_region(const ddprof::Dso &dso) {
  ddprof::RegionKey key(dso._filename, dso._pgoff, dso._end - dso._start + 1,
                        dso._type);
  const auto find_res = _region_map.find(key);
  LG_DBG("[DSO] Get region - %s", dso.to_string().c_str());
  if (find_res == _region_map.end()) {
    const auto insert_res = _region_map.emplace(
        std::move(key),
        ddprof::RegionHolder(dso._filename, dso._end - dso._start + 1,
                             dso._pgoff, dso._type));
    assert(insert_res.second);
    return insert_res.first->second;

  } else { // iterator contains a pair key / value
    return find_res->second;
  }
}

void DsoHdr::pid_free(int pid) {
  ddprof::DsoRange range = get_pid_range(pid);
  erase_range(range);
}

// Return false if nothing was added
// Return true if a dso was added
bool DsoHdr::pid_backpopulate(int pid) {
  ProcFileHolder proc_file_holder(pid);
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
  bool element_added = false;
  while (-1 != getline(&buf, &sz_buf, mpf)) {
    Dso dso = dso_from_procline(pid, buf);
    if (dso._pid == -1) { // invalid dso
      continue;
    }
    if ((insert_erase_overlap(std::move(dso))).second) {
      element_added = true;
    }
  }
  if (buf != NULL) {
    free(buf);
  }
  return element_added;
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
    throw ddprof::DDException(DD_SEVERROR, DD_WHAT_DSO);
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
