// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "live_allocation_snapshot.hpp"

#include "common_symbol_errors.hpp"
#include "ddog_profiling_utils.hpp"
#include "ddprof_file_info-i.hpp"
#include "logger.hpp"
#include "symbol_hdr.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <numeric>
#include <sys/uio.h>
#include <unistd.h>

namespace ddprof::live_alloc_snapshot {

namespace {

constexpr std::array<uint8_t, 4> k_magic = {'D', 'D', 'L', 'A'};
constexpr uint32_t k_version = 1;

// -- byte-cost estimation -----------------------------------------------------

constexpr std::size_t cost_string(std::string_view sv) {
  return sizeof(uint32_t) + sv.size();
}

std::size_t cost_funloc(const FunLocPortable &fl) {
  return sizeof(uint64_t) * 2 + // ip + elf_addr
      sizeof(uint32_t) +        // lineno
      sizeof(uint64_t) * 3 +    // map_low / map_high / map_offset
      cost_string(fl.fn_name) + cost_string(fl.fn_system_name) +
      cost_string(fl.fn_file) + cost_string(fl.map_filename) +
      cost_string(fl.map_build_id);
}

std::size_t cost_stack(const UnwindOutputPortable &uo) {
  std::size_t c = sizeof(int32_t) * 2 + // pid + tid
      cost_string(uo.container_id) + cost_string(uo.exe_name) +
      cost_string(uo.thread_name) + sizeof(uint32_t); // n_locs
  for (const auto &fl : uo.locs) {
    c += cost_funloc(fl);
  }
  return c;
}

constexpr std::size_t k_address_entry_cost =
    sizeof(uint64_t) + sizeof(int64_t) + sizeof(uint32_t);

std::size_t cost_pid_entry(const PidEntry &p) {
  return sizeof(int32_t) * 2 + sizeof(uint32_t) * 3 +
      p.addresses.size() * k_address_entry_cost;
}

constexpr std::size_t k_header_cost = sizeof(k_magic) + sizeof(uint32_t) * 5;

std::size_t estimate_size(const Snapshot &s) {
  std::size_t c = k_header_cost;
  for (const auto &st : s.stacks) {
    c += cost_stack(st);
  }
  for (const auto &p : s.pids) {
    c += cost_pid_entry(p);
  }
  return c;
}

// -- read-back helpers (libdatadog dictionary) --------------------------------

std::string copy_string(const ddog_prof_ProfilesDictionary *dict,
                        ddog_prof_StringId2 id) {
  if (!id) {
    return {};
  }
  return std::string(get_string(dict, id));
}

FunLocPortable funloc_to_portable(const FunLoc &fl,
                                  const SymbolHdr &symbol_hdr) {
  const auto *dict = symbol_hdr.profiles_dictionary();
  FunLocPortable out;
  out.ip = fl.ip;
  out.elf_addr = fl.elf_addr;

  if (fl.symbol_idx != k_symbol_idx_null &&
      static_cast<size_t>(fl.symbol_idx) < symbol_hdr._symbol_table.size()) {
    const Symbol &sym = symbol_hdr._symbol_table[fl.symbol_idx];
    out.lineno = sym._lineno;
    if (sym._function_id) {
      // Read fields directly from the libdatadog Function2 struct.
      // This is the same pattern already used by
      // get_location2_function_name() / get_location2_mapping_filename().
      out.fn_name = copy_string(dict, sym._function_id->name);
      out.fn_system_name = copy_string(dict, sym._function_id->system_name);
      out.fn_file = copy_string(dict, sym._function_id->file_name);
    }
  }

  if (fl.map_info_idx != k_mapinfo_idx_null &&
      static_cast<size_t>(fl.map_info_idx) < symbol_hdr._mapinfo_table.size()) {
    ddog_prof_MappingId2 mid = symbol_hdr._mapinfo_table[fl.map_info_idx];
    if (mid) {
      out.map_low = mid->memory_start;
      out.map_high = mid->memory_limit;
      out.map_offset = mid->file_offset;
      out.map_filename = copy_string(dict, mid->filename);
      out.map_build_id = copy_string(dict, mid->build_id);
    }
  }
  return out;
}

UnwindOutputPortable uo_to_portable(const UnwindOutput &uo,
                                    const SymbolHdr &symbol_hdr) {
  UnwindOutputPortable p;
  p.pid = uo.pid;
  p.tid = uo.tid;
  p.container_id = std::string(uo.container_id);
  p.exe_name = std::string(uo.exe_name);
  p.thread_name = std::string(uo.thread_name);
  p.locs.reserve(uo.locs.size());
  for (const auto &fl : uo.locs) {
    p.locs.emplace_back(funloc_to_portable(fl, symbol_hdr));
  }
  return p;
}

// -- binary writer / reader ---------------------------------------------------

class Writer {
public:
  explicit Writer(std::vector<uint8_t> &out) : _out(out) {}

  void u32(uint32_t v) { raw(&v, sizeof(v)); }
  void i32(int32_t v) { raw(&v, sizeof(v)); }
  void u64(uint64_t v) { raw(&v, sizeof(v)); }
  void i64(int64_t v) { raw(&v, sizeof(v)); }
  void str(std::string_view sv) {
    u32(static_cast<uint32_t>(sv.size()));
    raw(sv.data(), sv.size());
  }
  void raw(const void *p, std::size_t n) {
    const auto *bytes = static_cast<const uint8_t *>(p);
    _out.insert(_out.end(), bytes, bytes + n);
  }

private:
  std::vector<uint8_t> &_out;
};

class Reader {
public:
  Reader(const uint8_t *data, std::size_t size) : _p(data), _end(data + size) {}

  bool u32(uint32_t &v) { return raw(&v, sizeof(v)); }
  bool i32(int32_t &v) { return raw(&v, sizeof(v)); }
  bool u64(uint64_t &v) { return raw(&v, sizeof(v)); }
  bool i64(int64_t &v) { return raw(&v, sizeof(v)); }
  bool str(std::string &out) {
    uint32_t n = 0;
    if (!u32(n)) {
      return false;
    }
    if (static_cast<std::size_t>(_end - _p) < n) {
      return false;
    }
    out.assign(reinterpret_cast<const char *>(_p), n);
    _p += n;
    return true;
  }
  bool raw(void *dst, std::size_t n) {
    if (static_cast<std::size_t>(_end - _p) < n) {
      return false;
    }
    std::memcpy(dst, _p, n);
    _p += n;
    return true;
  }
  [[nodiscard]] bool eof() const { return _p == _end; }

private:
  const uint8_t *_p;
  const uint8_t *_end;
};

} // namespace

// -- capture ------------------------------------------------------------------

Snapshot capture_snapshot(const LiveAllocation &live_alloc,
                          const SymbolHdr &symbol_hdr, std::size_t max_bytes) {
  Snapshot snapshot;

  // Stage 1: dedupe stacks. The PprofStacks for different pids may produce
  // identical UnwindOutputPortable rows once converted; we keep them
  // separate at this stage and dedupe by raw pointer identity (each
  // PprofStacks::value_type address is unique within a pid).
  // For cross-pid identity we rely on the AddressMap pointer to map back.
  //
  // The simplest approach: emit one Portable per (pid_stack value_type
  // pointer) and look it up by that pointer when filling pid entries.
  struct StackKey {
    const LiveAllocation::PprofStacks::value_type *ptr;
  };
  std::unordered_map<const LiveAllocation::PprofStacks::value_type *, uint32_t>
      stack_idx_by_ptr;

  for (unsigned watcher_pos = 0;
       watcher_pos < live_alloc._watcher_vector.size(); ++watcher_pos) {
    const auto &pid_map = live_alloc._watcher_vector[watcher_pos];
    for (const auto &pid_kv : pid_map) {
      pid_t const pid = pid_kv.first;
      const auto &pid_stacks = pid_kv.second;

      PidEntry pid_entry;
      pid_entry.watcher_pos = static_cast<int>(watcher_pos);
      pid_entry.pid = pid;
      pid_entry.address_conflict_count = pid_stacks._address_conflict_count;
      pid_entry.tracked_address_count = pid_stacks._tracked_address_count;
      pid_entry.addresses.reserve(pid_stacks._address_map.size());

      for (const auto &addr_kv : pid_stacks._address_map) {
        const auto *stack_ptr = addr_kv.second._unique_stack;
        if (!stack_ptr || stack_ptr->first.locs.empty()) {
          continue;
        }
        uint32_t stack_idx;
        auto it = stack_idx_by_ptr.find(stack_ptr);
        if (it == stack_idx_by_ptr.end()) {
          stack_idx = static_cast<uint32_t>(snapshot.stacks.size());
          stack_idx_by_ptr.emplace(stack_ptr, stack_idx);
          snapshot.stacks.emplace_back(
              uo_to_portable(stack_ptr->first, symbol_hdr));
        } else {
          stack_idx = it->second;
        }
        pid_entry.addresses.push_back(
            {addr_kv.first, addr_kv.second._value, stack_idx});
      }
      if (!pid_entry.addresses.empty()) {
        snapshot.pids.emplace_back(std::move(pid_entry));
      }
    }
  }

  if (max_bytes == 0) {
    return snapshot;
  }

  // Stage 2: budget enforcement.
  std::size_t projected = estimate_size(snapshot);
  if (projected <= max_bytes) {
    return snapshot;
  }

  // 2a) Rank stacks by aggregate value across all pids (low value goes first).
  std::vector<int64_t> stack_total_value(snapshot.stacks.size(), 0);
  for (const auto &p : snapshot.pids) {
    for (const auto &a : p.addresses) {
      stack_total_value[a.stack_idx] += a.value;
    }
  }
  std::vector<uint32_t> stack_order(snapshot.stacks.size());
  std::iota(stack_order.begin(), stack_order.end(), 0u);
  std::sort(stack_order.begin(), stack_order.end(),
            [&](uint32_t a, uint32_t b) {
              return stack_total_value[a] < stack_total_value[b];
            });

  // Cost of the synthetic cleared stack we may need to introduce. It is a
  // single FunLocPortable with all strings empty -> small fixed cost.
  const UnwindOutputPortable cleared_stack_template{}; // empty
  const std::size_t cleared_stack_cost = cost_stack(cleared_stack_template);

  std::vector<bool> stack_dropped(snapshot.stacks.size(), false);
  bool cleared_stack_needed = false;
  for (uint32_t idx : stack_order) {
    if (projected <= max_bytes) {
      break;
    }
    std::size_t const saved = cost_stack(snapshot.stacks[idx]);
    projected -= saved;
    if (!cleared_stack_needed) {
      projected += cleared_stack_cost;
      cleared_stack_needed = true;
    }
    stack_dropped[idx] = true;
  }

  // 2b) If still over, drop pids from lowest aggregate value upwards.
  if (projected > max_bytes) {
    std::vector<int64_t> pid_total_value(snapshot.pids.size(), 0);
    for (size_t i = 0; i < snapshot.pids.size(); ++i) {
      for (const auto &a : snapshot.pids[i].addresses) {
        pid_total_value[i] += a.value;
      }
    }
    std::vector<uint32_t> pid_order(snapshot.pids.size());
    std::iota(pid_order.begin(), pid_order.end(), 0u);
    std::sort(pid_order.begin(), pid_order.end(), [&](uint32_t a, uint32_t b) {
      return pid_total_value[a] < pid_total_value[b];
    });
    std::vector<bool> pid_dropped(snapshot.pids.size(), false);
    for (uint32_t pidx : pid_order) {
      if (projected <= max_bytes) {
        break;
      }
      projected -= cost_pid_entry(snapshot.pids[pidx]);
      pid_dropped[pidx] = true;
      ++snapshot.dropped_pids;
    }
    if (snapshot.dropped_pids) {
      std::vector<PidEntry> kept;
      kept.reserve(snapshot.pids.size() - snapshot.dropped_pids);
      for (size_t i = 0; i < snapshot.pids.size(); ++i) {
        if (!pid_dropped[i]) {
          kept.emplace_back(std::move(snapshot.pids[i]));
        }
      }
      snapshot.pids = std::move(kept);
    }
  }

  // Apply stack drops: remap their address entries to k_cleared_stack_idx,
  // then compact the stacks[] vector and rewrite remaining stack_idx values.
  if (cleared_stack_needed) {
    for (auto &p : snapshot.pids) {
      for (auto &a : p.addresses) {
        if (stack_dropped[a.stack_idx]) {
          a.stack_idx = k_cleared_stack_idx;
          ++snapshot.cleared_addresses;
        }
      }
    }
    std::vector<uint32_t> remap(snapshot.stacks.size(), k_cleared_stack_idx);
    std::vector<UnwindOutputPortable> kept_stacks;
    kept_stacks.reserve(snapshot.stacks.size());
    for (size_t i = 0; i < snapshot.stacks.size(); ++i) {
      if (!stack_dropped[i]) {
        remap[i] = static_cast<uint32_t>(kept_stacks.size());
        kept_stacks.emplace_back(std::move(snapshot.stacks[i]));
      }
    }
    snapshot.stacks = std::move(kept_stacks);
    for (auto &p : snapshot.pids) {
      for (auto &a : p.addresses) {
        if (a.stack_idx != k_cleared_stack_idx) {
          a.stack_idx = remap[a.stack_idx];
        }
      }
    }
  }

  return snapshot;
}

// -- serialise / deserialise --------------------------------------------------

void serialize(const Snapshot &s, std::vector<uint8_t> &out) {
  out.clear();
  out.reserve(estimate_size(s));
  Writer w(out);
  w.raw(k_magic.data(), k_magic.size());
  w.u32(k_version);
  w.u32(static_cast<uint32_t>(s.stacks.size()));
  w.u32(static_cast<uint32_t>(s.pids.size()));
  w.u32(s.cleared_addresses);
  w.u32(s.dropped_pids);

  for (const auto &st : s.stacks) {
    w.i32(st.pid);
    w.i32(st.tid);
    w.str(st.container_id);
    w.str(st.exe_name);
    w.str(st.thread_name);
    w.u32(static_cast<uint32_t>(st.locs.size()));
    for (const auto &fl : st.locs) {
      w.u64(fl.ip);
      w.u64(fl.elf_addr);
      w.u32(fl.lineno);
      w.u64(fl.map_low);
      w.u64(fl.map_high);
      w.u64(fl.map_offset);
      w.str(fl.fn_name);
      w.str(fl.fn_system_name);
      w.str(fl.fn_file);
      w.str(fl.map_filename);
      w.str(fl.map_build_id);
    }
  }

  for (const auto &p : s.pids) {
    w.i32(p.watcher_pos);
    w.i32(p.pid);
    w.u32(p.address_conflict_count);
    w.u32(p.tracked_address_count);
    w.u32(static_cast<uint32_t>(p.addresses.size()));
    for (const auto &a : p.addresses) {
      w.u64(a.addr);
      w.i64(a.value);
      w.u32(a.stack_idx);
    }
  }
}

bool deserialize(const uint8_t *data, std::size_t size, Snapshot &out) {
  out = {};
  Reader r(data, size);
  std::array<uint8_t, 4> magic{};
  if (!r.raw(magic.data(), magic.size()) || magic != k_magic) {
    return false;
  }
  uint32_t version = 0;
  if (!r.u32(version) || version != k_version) {
    return false;
  }
  uint32_t n_stacks = 0;
  uint32_t n_pids = 0;
  if (!r.u32(n_stacks) || !r.u32(n_pids) || !r.u32(out.cleared_addresses) ||
      !r.u32(out.dropped_pids)) {
    return false;
  }
  out.stacks.resize(n_stacks);
  for (auto &st : out.stacks) {
    if (!r.i32(st.pid) || !r.i32(st.tid) || !r.str(st.container_id) ||
        !r.str(st.exe_name) || !r.str(st.thread_name)) {
      return false;
    }
    uint32_t n_locs = 0;
    if (!r.u32(n_locs)) {
      return false;
    }
    st.locs.resize(n_locs);
    for (auto &fl : st.locs) {
      if (!r.u64(fl.ip) || !r.u64(fl.elf_addr) || !r.u32(fl.lineno) ||
          !r.u64(fl.map_low) || !r.u64(fl.map_high) || !r.u64(fl.map_offset) ||
          !r.str(fl.fn_name) || !r.str(fl.fn_system_name) ||
          !r.str(fl.fn_file) || !r.str(fl.map_filename) ||
          !r.str(fl.map_build_id)) {
        return false;
      }
    }
  }
  out.pids.resize(n_pids);
  for (auto &p : out.pids) {
    if (!r.i32(p.watcher_pos) || !r.i32(p.pid) ||
        !r.u32(p.address_conflict_count) || !r.u32(p.tracked_address_count)) {
      return false;
    }
    uint32_t n_addr = 0;
    if (!r.u32(n_addr)) {
      return false;
    }
    p.addresses.resize(n_addr);
    for (auto &a : p.addresses) {
      if (!r.u64(a.addr) || !r.i64(a.value) || !r.u32(a.stack_idx)) {
        return false;
      }
    }
  }
  return r.eof();
}

bool write_to_fd(int fd, const Snapshot &snapshot) {
  if (fd < 0) {
    return false;
  }
  std::vector<uint8_t> buf;
  serialize(snapshot, buf);
  if (ftruncate(fd, 0) != 0) {
    return false;
  }
  if (lseek(fd, 0, SEEK_SET) == static_cast<off_t>(-1)) {
    return false;
  }
  std::size_t written = 0;
  while (written < buf.size()) {
    ssize_t const n = write(fd, buf.data() + written, buf.size() - written);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (n == 0) {
      return false;
    }
    written += static_cast<std::size_t>(n);
  }
  return true;
}

bool read_from_fd(int fd, Snapshot &out) {
  if (fd < 0) {
    return false;
  }
  off_t const sz = lseek(fd, 0, SEEK_END);
  if (sz <= 0) {
    return false;
  }
  if (lseek(fd, 0, SEEK_SET) == static_cast<off_t>(-1)) {
    return false;
  }
  std::vector<uint8_t> buf(static_cast<std::size_t>(sz));
  std::size_t read_total = 0;
  while (read_total < buf.size()) {
    ssize_t const n =
        read(fd, buf.data() + read_total, buf.size() - read_total);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (n == 0) {
      break;
    }
    read_total += static_cast<std::size_t>(n);
  }
  bool const ok = deserialize(buf.data(), read_total, out);
  // Consume-on-read: clear the memfd so a future worker that does not
  // produce its own snapshot does not inadvertently replay stale state.
  if (ftruncate(fd, 0) != 0) {
    LG_DBG("[live-alloc] ftruncate(snapshot_fd) failed: %d", errno);
  }
  if (lseek(fd, 0, SEEK_SET) == static_cast<off_t>(-1)) {
    LG_DBG("[live-alloc] lseek(snapshot_fd) failed: %d", errno);
  }
  return ok;
}

// -- restore ------------------------------------------------------------------

namespace {

// Build the synthetic [live-alloc cleared] UnwindOutput. Allocates one Symbol
// + one mapping in symbol_hdr.
UnwindOutput build_cleared_stack(SymbolHdr &symbol_hdr) {
  const auto *dict = symbol_hdr.profiles_dictionary();
  ddog_prof_FunctionId2 fn = intern_function(
      dict, k_common_frame_names[SymbolErrors::live_alloc_cleared], {});
  ddog_prof_MappingId2 mp = intern_mapping(dict, 0, 0, 0, {}, {});

  symbol_hdr._symbol_table.emplace_back(0u, fn);
  auto const symbol_idx =
      static_cast<SymbolIdx_t>(symbol_hdr._symbol_table.size() - 1);
  symbol_hdr._mapinfo_table.emplace_back(mp);
  auto const map_idx =
      static_cast<MapInfoIdx_t>(symbol_hdr._mapinfo_table.size() - 1);

  UnwindOutput uo;
  uo.locs.push_back(
      {0, 0, /*file_info_id*/ k_file_info_undef, symbol_idx, map_idx});
  // Empty pid/tid/strings: distinct from any natural unwind output.
  return uo;
}

UnwindOutput portable_to_uo(const UnwindOutputPortable &p,
                            SymbolHdr &symbol_hdr, LiveAllocation &live_alloc) {
  const auto *dict = symbol_hdr.profiles_dictionary();
  UnwindOutput uo;
  uo.pid = p.pid;
  uo.tid = p.tid;
  uo.container_id = live_alloc.intern_restored_string(p.container_id);
  uo.exe_name = live_alloc.intern_restored_string(p.exe_name);
  uo.thread_name = live_alloc.intern_restored_string(p.thread_name);
  uo.locs.reserve(p.locs.size());
  for (const auto &fl : p.locs) {
    ddog_prof_MappingId2 mid =
        intern_mapping(dict, fl.map_low, fl.map_high, fl.map_offset,
                       fl.map_filename, fl.map_build_id);
    symbol_hdr._mapinfo_table.emplace_back(mid);
    auto const map_idx =
        static_cast<MapInfoIdx_t>(symbol_hdr._mapinfo_table.size() - 1);

    ddog_prof_FunctionId2 fn =
        intern_function(dict, fl.fn_name, fl.fn_file, fl.fn_system_name);
    symbol_hdr._symbol_table.emplace_back(fl.lineno, fn);
    auto const sym_idx =
        static_cast<SymbolIdx_t>(symbol_hdr._symbol_table.size() - 1);

    uo.locs.push_back(
        {fl.ip, fl.elf_addr, k_file_info_undef, sym_idx, map_idx});
  }
  return uo;
}

} // namespace

void restore_snapshot(const Snapshot &snapshot, LiveAllocation &live_alloc,
                      SymbolHdr &symbol_hdr) {
  // Pre-build restored UnwindOutputs for every snapshot stack.
  std::vector<UnwindOutput> rebuilt;
  rebuilt.reserve(snapshot.stacks.size());
  for (const auto &p : snapshot.stacks) {
    rebuilt.emplace_back(portable_to_uo(p, symbol_hdr, live_alloc));
  }

  // Build the synthetic cleared stack lazily, only if referenced.
  bool need_cleared = false;
  for (const auto &p : snapshot.pids) {
    for (const auto &a : p.addresses) {
      if (a.stack_idx == k_cleared_stack_idx) {
        need_cleared = true;
        break;
      }
    }
    if (need_cleared) {
      break;
    }
  }
  UnwindOutput cleared_uo;
  if (need_cleared) {
    cleared_uo = build_cleared_stack(symbol_hdr);
  }

  for (const auto &p : snapshot.pids) {
    for (const auto &a : p.addresses) {
      const UnwindOutput &uo = (a.stack_idx == k_cleared_stack_idx)
          ? cleared_uo
          : rebuilt[a.stack_idx];
      live_alloc.register_allocation(uo, a.addr, a.value, p.watcher_pos, p.pid);
    }
    // Carry over the library/profiler tracked-address counters so the
    // post-restore mismatch warning is anchored at the same baseline.
    auto &pid_map = access_resize(live_alloc._watcher_vector, p.watcher_pos);
    auto &pid_stacks = pid_map[p.pid];
    pid_stacks._address_conflict_count = p.address_conflict_count;
    pid_stacks._tracked_address_count = p.tracked_address_count;
  }

  if (snapshot.cleared_addresses || snapshot.dropped_pids) {
    LG_NTC("[live-alloc] Snapshot degraded: cleared_addresses=%u "
           "dropped_pids=%u",
           snapshot.cleared_addresses, snapshot.dropped_pids);
  }
}

} // namespace ddprof::live_alloc_snapshot
