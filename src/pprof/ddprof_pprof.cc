// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "pprof/ddprof_pprof.hpp"

#include "base_frame_symbol_lookup.hpp"
#include "common_symbol_errors.hpp"
#include "ddog_profiling_utils.hpp"
#include "ddprof_defs.hpp"
#include "ddres.hpp"
#include "defer.hpp"
#include "pevent_lib.hpp"
#include "symbol_hdr.hpp"
#include "symbolizer.hpp"

#include <absl/strings/str_format.h>
#include <absl/strings/substitute.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <datadog/common.h>
#include <datadog/profiling.h>
#include <span>
#include <string_view>

// sv operator
using namespace std::string_view_literals;

namespace ddprof {

namespace {
constexpr size_t k_max_pprof_labels{8};

constexpr std::string_view k_container_id_label = "container_id"sv;
constexpr std::string_view k_process_id_label = "process_id"sv;
constexpr std::string_view k_process_name_label = "process_name"sv;
constexpr std::string_view k_thread_id_label = "thread id"sv;
constexpr std::string_view k_thread_name_label = "thread_name"sv;
constexpr std::string_view k_tracepoint_label = "tracepoint_type"sv;

// Maps a ddog_prof_SampleType to the kebab-case name used in debug log output
// (must match what simple_malloc-ut.sh greps for).
constexpr const char *sample_type_name(uint32_t raw_type) {
  switch (raw_type) {
  case DDOG_PROF_SAMPLE_TYPE_SAMPLE:
    return "sample";
  case DDOG_PROF_SAMPLE_TYPE_TRACEPOINT:
    return "tracepoint";
  case DDOG_PROF_SAMPLE_TYPE_CPU_TIME:
    return "cpu-time";
  case DDOG_PROF_SAMPLE_TYPE_CPU_SAMPLES:
    return "cpu-samples";
  case DDOG_PROF_SAMPLE_TYPE_ALLOC_SPACE:
    return "alloc-space";
  case DDOG_PROF_SAMPLE_TYPE_ALLOC_SAMPLES:
    return "alloc-samples";
  case DDOG_PROF_SAMPLE_TYPE_INUSE_SPACE:
    return "inuse-space";
  case DDOG_PROF_SAMPLE_TYPE_INUSE_OBJECTS:
    return "inuse-objects";
  default:
    return "unknown";
  }
}

// Verify that sample_type_name() returns the strings the backend expects.
static_assert(std::string_view(sample_type_name(
                  DDOG_PROF_SAMPLE_TYPE_CPU_TIME)) == "cpu-time");
static_assert(std::string_view(sample_type_name(
                  DDOG_PROF_SAMPLE_TYPE_CPU_SAMPLES)) == "cpu-samples");
static_assert(std::string_view(sample_type_name(
                  DDOG_PROF_SAMPLE_TYPE_ALLOC_SPACE)) == "alloc-space");
static_assert(std::string_view(sample_type_name(
                  DDOG_PROF_SAMPLE_TYPE_ALLOC_SAMPLES)) == "alloc-samples");
static_assert(std::string_view(sample_type_name(
                  DDOG_PROF_SAMPLE_TYPE_INUSE_SPACE)) == "inuse-space");
static_assert(std::string_view(sample_type_name(
                  DDOG_PROF_SAMPLE_TYPE_INUSE_OBJECTS)) == "inuse-objects");
static_assert(std::string_view(sample_type_name(
                  DDOG_PROF_SAMPLE_TYPE_TRACEPOINT)) == "tracepoint");
static_assert(std::string_view(
                  sample_type_name(DDOG_PROF_SAMPLE_TYPE_SAMPLE)) == "sample");

// Upper bound on distinct ddog_prof_SampleType slots (sum + live types +
// their count companions across all watcher kinds).
constexpr int k_max_value_types = 16;



std::string_view pid_str(pid_t pid,
                         std::unordered_map<pid_t, std::string> &pid_strs);

void init_dict_label_key_ids(DDProfPProf::DictLabelKeyIds &label_keys,
                             const ddog_prof_ProfilesDictionary *dict) {
  label_keys.container_id = intern_string(dict, k_container_id_label);
  label_keys.process_id = intern_string(dict, k_process_id_label);
  label_keys.process_name = intern_string(dict, k_process_name_label);
  label_keys.thread_id = intern_string(dict, k_thread_id_label);
  label_keys.thread_name = intern_string(dict, k_thread_name_label);
  label_keys.tracepoint_type = intern_string(dict, k_tracepoint_label);
}

size_t prepare_labels2(const UnwindOutput &uw_output,
                       const PerfWatcher &watcher,
                       std::unordered_map<pid_t, std::string> &pid_strs,
                       const DDProfPProf::DictLabelKeyIds &label_keys,
                       std::span<ddog_prof_Label2> labels) {
  size_t labels_num = 0;

  auto push_label = [&](ddog_prof_StringId2 key_id, std::string_view value) {
    labels[labels_num++] = {
        .key = key_id,
        .str = to_CharSlice(value),
        .num = 0,
        .num_unit = {.ptr = nullptr, .len = 0},
    };
  };

  if (!uw_output.container_id.empty()) {
    push_label(label_keys.container_id, uw_output.container_id);
  }

  if (!watcher.suppress_pid || !watcher.suppress_tid) {
    push_label(label_keys.process_id, pid_str(uw_output.pid, pid_strs));
  }
  if (!watcher.suppress_tid) {
    push_label(label_keys.thread_id, pid_str(uw_output.tid, pid_strs));
  }
  if (watcher_has_tracepoint(&watcher)) {
    if (!watcher.tracepoint_label.empty()) {
      push_label(label_keys.tracepoint_type, watcher.tracepoint_label);
    } else {
      push_label(label_keys.tracepoint_type, watcher.tracepoint_event);
    }
  }
  if (!uw_output.exe_name.empty()) {
    push_label(label_keys.process_name, uw_output.exe_name);
  }
  if (!uw_output.thread_name.empty()) {
    push_label(label_keys.thread_name, uw_output.thread_name);
  }

  DDPROF_DCHECK_FATAL(labels_num <= labels.size(),
                      "pprof_aggregate - label buffer exceeded");
  return labels_num;
}

std::string_view pid_str(pid_t pid,
                         std::unordered_map<pid_t, std::string> &pid_strs) {
  auto it = pid_strs.find(pid);
  if (it != pid_strs.end()) {
    return it->second;
  }
  const auto pair = pid_strs.emplace(pid, std::to_string(pid));
  return pair.first->second;
}

bool is_ld(const std::string_view path) {
  // path is expected to not contain slashes
  assert(path.rfind('/') == std::string::npos);

  return path.starts_with("ld-");
}

bool is_stack_complete(std::span<const ddog_prof_Location2> locations,
                       const ddog_prof_ProfilesDictionary *dict) {
  static constexpr std::array s_expected_root_frames{
      // Consider empty as OK (to avoid false incomplete frames)
      // If we have no symbols, we could still retrieve them in the backend.
      ""sv,
      "clone"sv,
      "__clone"sv,
      "__clone3"sv,
      "_exit"sv,
      "gnome-shell"sv,
      "main"sv,
      "runtime.goexit.abi0"sv,
      "runtime.systemstack.abi0"sv,
      "_start"sv,
      "start_thread"sv,
      "start_task"sv};

  if (locations.empty()) {
    return false;
  }

  const auto &root_loc = locations.back();
  const std::string_view root_mapping =
      get_location2_mapping_filename(dict, root_loc);
  // If we are in ld.so (eg. during lib init before main) consider the stack as
  // complete
  if (is_ld(root_mapping)) {
    return true;
  }

  const std::string_view root_func =
      get_location2_function_name(dict, root_loc);
  return std::find(s_expected_root_frames.begin(), s_expected_root_frames.end(),
                   root_func) != s_expected_root_frames.end();
}

void ddprof_print_sample(std::span<const ddog_prof_Location2> locations,
                         const ddog_prof_ProfilesDictionary *dict,
                         uint64_t value, pid_t pid, pid_t tid,
                         EventAggregationModePos value_mode_pos,
                         const PerfWatcher &watcher) {

  const char *const type_name =
      sample_type_name(watcher.sample_type_info.sample_types[value_mode_pos]);
  std::string buf =
      absl::Substitute("sample[type=$0;pid=$1;tid=$2] ", type_name, pid, tid);

  for (auto loc_it = locations.rbegin(); loc_it != locations.rend(); ++loc_it) {
    if (loc_it != locations.rbegin()) {
      buf += ";";
    }
    const std::string_view function_name =
        get_location2_function_name(dict, *loc_it);
    std::string_view source_file;
    if (loc_it->function) {
      source_file = get_string(dict, loc_it->function->file_name);
    }
    if (!function_name.empty()) {
      // Append the function name, trimming at the first '(' if present.
      buf += function_name.substr(0, function_name.find('('));
    } else if (!source_file.empty()) {
      // Append the file name, showing only the file and not the full path.
      auto pos = source_file.rfind('/');
      buf += "(";
      buf += source_file.substr(pos == std::string_view::npos ? 0 : pos + 1);
      buf += ")";
    } else {
      // If neither function name nor source file is available, show addresses.
      absl::StrAppendFormat(&buf, "%#x", loc_it->address);
    }

    // Include line number if available and greater than zero.
    if (loc_it->line > 0) {
      absl::StrAppendFormat(&buf, ":%d", loc_it->line);
    }
  }

  PRINT_NFO("%s %ld", buf.c_str(), value);
}

// Slot registry: maps ddog_prof_SampleType values to pprof value-array indices.
// Uses linear search — at most k_max_value_types unique types in practice.
struct SlotRegistry {
  ddog_prof_SampleType types[k_max_value_types] = {};
  int count = 0;

  int ensure(uint32_t raw_type) {
    const auto t = static_cast<ddog_prof_SampleType>(raw_type);
    for (int i = 0; i < count; ++i) {
      if (types[i] == t) {
        return i;
      }
    }
    assert(count < k_max_value_types);
    types[count] = t;
    return count++;
  }

  [[nodiscard]] ddog_prof_Slice_SampleType slice() const {
    return {.ptr = types, .len = static_cast<uintptr_t>(count)};
  }
};


std::span<const FunLoc> adjust_locations(const PerfWatcher *watcher,
                                         std::span<const FunLoc> locs) {
  if (watcher->options.nb_frames_to_skip < locs.size()) {
    return locs.subspan(watcher->options.nb_frames_to_skip);
  }
  // Keep the last frame. In the case of stacks that we could not unwind
  // We will still have the `binary_name` frame
  if (locs.size() >= 2) {
    return locs.subspan(locs.size() - 1);
  }
  return locs;
}

DDRes process_symbolization(
    std::span<const FunLoc> locs, const SymbolHdr &symbol_hdr,
    const FileInfoVector &file_infos, Symbolizer *symbolizer,
    const ddog_prof_ProfilesDictionary *dict,
    std::array<ddog_prof_Location2, kMaxStackDepth> &locations2_buff,
    Symbolizer::BlazeResultsWrapper &session_results, unsigned &write_index) {
  unsigned index = 0;

  const ddprof::SymbolTable &symbol_table = symbol_hdr._symbol_table;
  const ddprof::MapInfoTable &mapinfo_table = symbol_hdr._mapinfo_table;

  // The -1 on size is because the last frame is binary.
  // We wait for the incomplete frame to be added if needed.
  // By removing the incomplete frame and pushing logic to BE
  // we can simplify this loop
  while (index < locs.size() - 1 && write_index < locations2_buff.size()) {
    if (locs[index].symbol_idx != k_symbol_idx_null) {
      // Location already symbolized
      const FunLoc &loc = locs[index];
      write_location2(loc, mapinfo_table[loc.map_info_idx],
                      symbol_table[loc.symbol_idx],
                      &locations2_buff[write_index]);
      ++write_index;
      ++index;
      continue;
    }

    if (locs[index].file_info_id <= k_file_info_error) {
      // Invalid file info ID, error case
      DDPROF_DCHECK_FATAL(false,
                          "Error in pprof symbolization (no file provided)");
      ++index;
      continue;
    }

    const FileInfoId_t file_id = locs[index].file_info_id;
    const std::string &current_file_path = file_infos[file_id].get_path();
    std::vector<uintptr_t> elf_addresses;

    // Collect all consecutive locations for the same file
    const unsigned start_index = index;
    while (index < locs.size() && locs[index].file_info_id == file_id) {
      elf_addresses.push_back(locs[index].elf_addr);
      ++index;
      if (index < locs.size() && locs[index].symbol_idx != k_symbol_idx_null) {
        break; // Stop if we find a symbolized location
      }
    }
    // Perform symbolization for all collected addresses
    const DDRes res = symbolizer->symbolize_pprof(
        elf_addresses, file_id, current_file_path,
        mapinfo_table[locs[start_index].map_info_idx], dict,
        std::span<ddog_prof_Location2>{locations2_buff}, write_index,
        session_results);
    if (IsDDResNotOK(res)) {
      if (IsDDResFatal(res)) {
        DDRES_RETURN_ERROR_LOG(DD_WHAT_SYMBOLIZER, "Failed to symbolize pprof");
      }
      // Export the symbol with what we were able to symbolize
      break;
    }
  }

  // check if unwinding stops on a frame that makes sense
  if (write_index < (kMaxStackDepth - 1) && write_index >= 1 &&
      !is_stack_complete(
          std::span<ddog_prof_Location2>{locations2_buff.data(), write_index},
          dict)) {
    // Write a common frame to indicate an incomplete stack
    ddog_prof_Location2 &incomplete_loc = locations2_buff[write_index];
    incomplete_loc.mapping = nullptr;
    incomplete_loc.function =
        intern_function(dict, k_common_frame_names[incomplete_stack], {});
    incomplete_loc.address = 0;
    incomplete_loc.line = 0;
    ++write_index;
  }

  // Write the binary frame if it exists and is valid
  if (write_index < kMaxStackDepth &&
      locs.back().symbol_idx != k_symbol_idx_null) {
    const FunLoc &loc = locs.back();
    write_location2(loc, mapinfo_table[loc.map_info_idx],
                    symbol_table[loc.symbol_idx],
                    &locations2_buff[write_index]);
    ++write_index;
  }
  return {};
}

} // namespace

DDRes pprof_create_profile(DDProfPProf *pprof, DDProfContext &ctx,
                           const ddog_prof_ProfilesDictionaryHandle *dict) {
  SlotRegistry slots;
  PerfWatcher *default_watcher = nullptr;

  for (auto &w : ctx.watchers) {
    // Reset indices from any previous profile creation
    for (auto &pi : w.pprof_indices) {
      pi = {};
    }

    if (!is_pprof_active(w.sample_type_info)) {
      continue;
    }

    if (!default_watcher) {
      default_watcher = &w;
    }

    for (int m = 0; m < kNbEventAggregationModes; ++m) {
      if (!Any(w.aggregation_mode &
               static_cast<EventAggregationMode>(1 << m))) {
        continue;
      }
      const uint32_t sample_t = w.sample_type_info.sample_types[m];
      if (sample_t == k_stype_none) {
        continue;
      }
      w.pprof_indices[m].pprof_index = slots.ensure(sample_t);
      const uint32_t count_t = w.sample_type_info.count_types[m];
      if (count_t != k_stype_none) {
        w.pprof_indices[m].pprof_count_index = slots.ensure(count_t);
      }
    }
  }

  pprof->_nb_values = slots.count;
  const ddog_prof_Slice_SampleType sample_types = slots.slice();
  ddog_prof_Period period{};
  if (pprof->_nb_values > 0) {
    if (!default_watcher) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "Unable to find default watcher");
    }
    // Populate the default.  If we have a frequency, assume it is given in
    // hertz and convert to a period in nanoseconds.  This is broken for many
    // event-based types (but providing frequency would also be broken in those
    // cases)
    int64_t default_period = default_watcher->sample_period;
    if (default_watcher->options.is_freq) {
      default_period =
          std::chrono::nanoseconds(std::chrono::seconds{1}).count() /
          default_period;
    }
    int default_index = -1;
    for (int m = 0; m < kNbEventAggregationModes && default_index == -1; ++m) {
      default_index = default_watcher->pprof_indices[m].pprof_index;
    }
    if (default_index == -1) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF,
                             "Unable to find default watcher's value");
    }
    period = {
        .sample_type = sample_types.ptr[default_index],
        .value = default_period,
    };
  }

  ddog_prof_Status status = ddog_prof_Profile_with_dictionary(
      &pprof->_profile, dict, sample_types,
      slots.count > 0 ? &period : nullptr);

  if (status.err != nullptr) {
    defer { ddog_prof_Status_drop(&status); };
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "Unable to create new profile: %s",
                           status.err);
  }

  init_dict_label_key_ids(pprof->_dict_label_key_ids, *dict);

  // Add relevant tags
  {
    bool const include_kernel =
        pevent_include_kernel_events(&ctx.worker_ctx.pevent_hdr);
    pprof->_tags.emplace_back(std::string("include_kernel"),
                              include_kernel ? std::string("true")
                                             : std::string("false"));
  }
  {
    // custom context
    // Allow the data to be split by container-id
    pprof->_tags.emplace_back(std::string("ddprof.custom_ctx"),
                              std::string("container_id"));
  }

  if (ctx.params.remote_symbolization) {
    pprof->_tags.emplace_back(std::string("remote_symbols"),
                              std::string("yes"));
  }

#ifdef __x86_64__
  pprof->_tags.emplace_back(std::string("cpu_arch"), std::string("amd64"));
#elif defined(__aarch64__)
  pprof->_tags.emplace_back(std::string("cpu_arch"), std::string("arm64"));
#endif

  return {};
}

DDRes pprof_free_profile(DDProfPProf *pprof) {
  ddog_prof_Profile_drop(&pprof->_profile);
  pprof->_profile = {};
  pprof->_nb_values = 0;
  pprof->_pid_str.clear();
  return {};
}

DDRes pprof_reset(DDProfPProf *pprof) {
  auto res = ddog_prof_Profile_reset(&pprof->_profile);
  if (res.tag != DDOG_PROF_PROFILE_RESULT_OK) {
    defer { ddog_Error_drop(&res.err); };
    auto msg = ddog_Error_message(&res.err);
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "Unable to reset profile: %*s",
                           static_cast<int>(msg.len), msg.ptr);
  }
  pprof->_pid_str.clear();
  return {};
}

DDRes pprof_aggregate_interned_sample(
    const UnwindOutput *uw_output, const SymbolHdr &symbol_hdr,
    const DDProfValuePack &pack, const PerfWatcher *watcher,
    const FileInfoVector &file_infos, bool show_samples,
    EventAggregationModePos value_pos, Symbolizer *symbolizer,
    DDProfPProf *pprof) {
  const PProfIndices &pprof_indices = watcher->pprof_indices[value_pos];
  ddog_prof_Profile *profile = &pprof->_profile;
  int64_t values[k_max_value_types] = {};
  assert(pprof_indices.pprof_index != -1);
  values[pprof_indices.pprof_index] = pack.value;
  if (pprof_indices.pprof_count_index != -1) {
    values[pprof_indices.pprof_count_index] = pack.count;
  }

  std::array<ddog_prof_Location2, kMaxStackDepth> locations2_buff;
  std::span locs{uw_output->locs};
  locs = adjust_locations(watcher, locs);

  Symbolizer::BlazeResultsWrapper session_results;
  unsigned write_index = 0;
  DDRES_CHECK_FWD(
      process_symbolization(locs, symbol_hdr, file_infos, symbolizer,
                            symbol_hdr.profiles_dictionary(), locations2_buff,
                            session_results, write_index));

  std::array<ddog_prof_Label2, k_max_pprof_labels> labels{};
  const size_t labels_num =
      prepare_labels2(*uw_output, *watcher, pprof->_pid_str,
                      pprof->_dict_label_key_ids, std::span{labels});

  if (show_samples) {
    ddprof_print_sample(std::span{locations2_buff.data(), write_index},
                        symbol_hdr.profiles_dictionary(), pack.value,
                        uw_output->pid, uw_output->tid, value_pos, *watcher);
  }

  ddog_prof_Sample2 const sample = {
      .locations = {.ptr = locations2_buff.data(), .len = write_index},
      .values = {.ptr = values, .len = pprof->_nb_values},
      .labels = {.ptr = labels.data(), .len = labels_num},
  };

  ddog_prof_Status res =
      ddog_prof_Profile_add2(profile, sample, pack.timestamp);
  if (res.err != nullptr) {
    defer { ddog_prof_Status_drop(&res); };
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "Unable to add profile: %s", res.err);
  }
  return {};
}
} // namespace ddprof
