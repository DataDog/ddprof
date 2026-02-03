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

constexpr int k_max_value_types =
    DDPROF_PWT_LENGTH * static_cast<int>(kNbEventAggregationModes);

struct ActiveIdsResult {
  EventAggregationMode output_mode[DDPROF_PWT_LENGTH] = {};
  PerfWatcher *default_watcher = nullptr;
};

struct LabelKeyIds {
  const ddog_prof_ProfilesDictionary *dict = nullptr;
  ddog_prof_StringId2 container_id = DDOG_PROF_STRINGID2_EMPTY;
  ddog_prof_StringId2 process_id = DDOG_PROF_STRINGID2_EMPTY;
  ddog_prof_StringId2 process_name = DDOG_PROF_STRINGID2_EMPTY;
  ddog_prof_StringId2 thread_id = DDOG_PROF_STRINGID2_EMPTY;
  ddog_prof_StringId2 thread_name = DDOG_PROF_STRINGID2_EMPTY;
  ddog_prof_StringId2 tracepoint_type = DDOG_PROF_STRINGID2_EMPTY;
};

const LabelKeyIds &
get_label_key_ids(const ddog_prof_ProfilesDictionary *dict) {
  static LabelKeyIds ids{};
  if (ids.dict != dict) {
    ids = {};
    ids.dict = dict;
    ids.container_id = intern_string(dict, k_container_id_label);
    ids.process_id = intern_string(dict, k_process_id_label);
    ids.process_name = intern_string(dict, k_process_name_label);
    ids.thread_id = intern_string(dict, k_thread_id_label);
    ids.thread_name = intern_string(dict, k_thread_name_label);
    ids.tracepoint_type = intern_string(dict, k_tracepoint_label);
  }
  return ids;
}

std::string_view pid_str(pid_t pid,
                         std::unordered_map<pid_t, std::string> &pid_strs);

std::string_view to_string_view(ddog_CharSlice slice) {
  if (!slice.ptr || slice.len == 0) {
    return {};
  }
  return {slice.ptr, slice.len};
}

ddog_prof_StringId intern_profile_string(ddog_prof_Profile *profile,
                                         std::string_view value) {
  if (value.empty()) {
    return ddog_prof_Profile_interned_empty_string();
  }
  ddog_prof_StringId_Result res =
      ddog_prof_Profile_intern_string(profile, to_CharSlice(value));
  if (res.tag != DDOG_PROF_STRING_ID_RESULT_OK_GENERATIONAL_ID_STRING_ID) {
    ddog_Error_drop(&res.err);
    return ddog_prof_Profile_interned_empty_string();
  }
  return res.ok;
}

DDRes intern_profile_labelset(
    ddog_prof_Profile *profile, const UnwindOutput &uw_output,
    const PerfWatcher &watcher,
    std::unordered_map<pid_t, std::string> &pid_strs,
    ddog_prof_LabelSetId *labelset_id) {
  std::array<ddog_prof_LabelId, k_max_pprof_labels> labels{};
  size_t labels_num = 0;

  auto push_label = [&](std::string_view key, std::string_view value) -> DDRes {
    ddog_prof_StringId key_id = intern_profile_string(profile, key);
    ddog_prof_StringId value_id = intern_profile_string(profile, value);
    ddog_prof_LabelId_Result label_res =
        ddog_prof_Profile_intern_label_str(profile, key_id, value_id);
    if (label_res.tag != DDOG_PROF_LABEL_ID_RESULT_OK_GENERATIONAL_ID_LABEL_ID) {
      ddog_Error_drop(&label_res.err);
      return ddres_error(DD_WHAT_PPROF);
    }
    labels[labels_num++] = label_res.ok;
    return {};
  };

  if (!uw_output.container_id.empty()) {
    DDRES_CHECK_FWD(
        push_label(k_container_id_label, uw_output.container_id));
  }

  if (!watcher.suppress_pid || !watcher.suppress_tid) {
    DDRES_CHECK_FWD(push_label(k_process_id_label,
                               pid_str(uw_output.pid, pid_strs)));
  }
  if (!watcher.suppress_tid) {
    DDRES_CHECK_FWD(push_label(k_thread_id_label,
                               pid_str(uw_output.tid, pid_strs)));
  }
  if (watcher_has_tracepoint(&watcher)) {
    if (!watcher.tracepoint_label.empty()) {
      DDRES_CHECK_FWD(
          push_label(k_tracepoint_label, watcher.tracepoint_label));
    } else {
      DDRES_CHECK_FWD(
          push_label(k_tracepoint_label, watcher.tracepoint_event));
    }
  }
  if (!uw_output.exe_name.empty()) {
    DDRES_CHECK_FWD(push_label(k_process_name_label, uw_output.exe_name));
  }
  if (!uw_output.thread_name.empty()) {
    DDRES_CHECK_FWD(push_label(k_thread_name_label, uw_output.thread_name));
  }

  DDPROF_DCHECK_FATAL(labels_num <= labels.size(),
                      "pprof_aggregate - label buffer exceeded");
  ddog_prof_LabelSetId_Result labelset_res = ddog_prof_Profile_intern_labelset(
      profile,
      {.ptr = labels.data(), .len = static_cast<uintptr_t>(labels_num)});
  if (labelset_res.tag !=
      DDOG_PROF_LABEL_SET_ID_RESULT_OK_GENERATIONAL_ID_LABEL_SET_ID) {
    ddog_Error_drop(&labelset_res.err);
    return ddres_error(DD_WHAT_PPROF);
  }
  *labelset_id = labelset_res.ok;
  return {};
}

DDRes intern_profile_location_id(ddog_prof_Profile *profile,
                                 const ddog_prof_Location &location,
                                 ddog_prof_LocationId *location_id) {
  ddog_prof_StringId fn_name =
      intern_profile_string(profile, to_string_view(location.function.name));
  ddog_prof_StringId fn_system = intern_profile_string(
      profile, to_string_view(location.function.system_name));
  ddog_prof_StringId fn_file =
      intern_profile_string(profile, to_string_view(location.function.filename));
  ddog_prof_FunctionId_Result function_res =
      ddog_prof_Profile_intern_function(profile, fn_name, fn_system, fn_file);
  if (function_res.tag !=
      DDOG_PROF_FUNCTION_ID_RESULT_OK_GENERATIONAL_ID_FUNCTION_ID) {
    ddog_Error_drop(&function_res.err);
    return ddres_error(DD_WHAT_PPROF);
  }

  ddog_prof_StringId map_filename =
      intern_profile_string(profile, to_string_view(location.mapping.filename));
  ddog_prof_StringId map_build_id =
      intern_profile_string(profile, to_string_view(location.mapping.build_id));
  ddog_prof_MappingId_Result mapping_res = ddog_prof_Profile_intern_mapping(
      profile, location.mapping.memory_start, location.mapping.memory_limit,
      location.mapping.file_offset, map_filename, map_build_id);
  if (mapping_res.tag !=
      DDOG_PROF_MAPPING_ID_RESULT_OK_GENERATIONAL_ID_MAPPING_ID) {
    ddog_Error_drop(&mapping_res.err);
    return ddres_error(DD_WHAT_PPROF);
  }

  ddog_prof_LocationId_Result loc_res =
      ddog_prof_Profile_intern_location_with_mapping_id(
          profile, mapping_res.ok, function_res.ok, location.address,
          location.line);
  if (loc_res.tag !=
      DDOG_PROF_LOCATION_ID_RESULT_OK_GENERATIONAL_ID_LOCATION_ID) {
    ddog_Error_drop(&loc_res.err);
    return ddres_error(DD_WHAT_PPROF);
  }
  *location_id = loc_res.ok;
  return {};
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

bool is_stack_complete(std::span<const ddog_prof_Location> locations) {
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
  const std::string_view root_mapping{root_loc.mapping.filename.ptr,
                                      root_loc.mapping.filename.len};
  // If we are in ld.so (eg. during lib init before main) consider the stack as
  // complete
  if (is_ld(root_mapping)) {
    return true;
  }

  const std::string_view root_func =
      std::string_view(root_loc.function.name.ptr, root_loc.function.name.len);
  return std::find(s_expected_root_frames.begin(), s_expected_root_frames.end(),
                   root_func) != s_expected_root_frames.end();
}

void ddprof_print_sample(std::span<const ddog_prof_Location> locations,
                         uint64_t value, pid_t pid, pid_t tid,
                         EventAggregationModePos value_mode_pos,
                         const PerfWatcher &watcher) {

  const char *sample_name =
      sample_type_name_from_idx(watcher.sample_type_id, value_mode_pos);
  std::string buf =
      absl::Substitute("sample[type=$0;pid=$1;tid=$2] ", sample_name, pid, tid);

  for (auto loc_it = locations.rbegin(); loc_it != locations.rend(); ++loc_it) {
    if (loc_it != locations.rbegin()) {
      buf += ";";
    }
    // Access function name and source file from location
    const std::string_view function_name{loc_it->function.name.ptr,
                                         loc_it->function.name.len};
    const std::string_view source_file{loc_it->function.filename.ptr,
                                       loc_it->function.filename.len};
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

// Figure out which sample_type_ids are used by active watchers
// We also record the watcher with the lowest valid sample_type id, since that
// will serve as the default for the pprof
DDRes get_active_ids(std::span<PerfWatcher> watchers, ActiveIdsResult &result) {

  for (unsigned i = 0; i < watchers.size(); ++i) {
    const int sample_type_id = watchers[i].sample_type_id;
    const int count_id = sample_type_id_to_count_sample_type_id(sample_type_id);
    if (sample_type_id < 0 || sample_type_id == DDPROF_PWT_NOCOUNT ||
        sample_type_id >= DDPROF_PWT_LENGTH) {
      if (sample_type_id != DDPROF_PWT_NOCOUNT) {
        DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF,
                               "Watcher %s (%d) has invalid sample_type_id %d",
                               watchers[i].desc.c_str(), i, sample_type_id);
      }
      continue;
    }

    if (!result.default_watcher ||
        sample_type_id <= result.default_watcher->sample_type_id) {
      result.default_watcher = &watchers[i]; // update default
    }
    result.output_mode[sample_type_id] |= watchers[i].aggregation_mode;
    if (count_id != DDPROF_PWT_NOCOUNT) {
      // if the count is valid, update mask for it
      result.output_mode[count_id] |= watchers[i].aggregation_mode;
    }
  }
  return {};
}

class ProfValueTypes {
private:
  int watcher_type_to_pprof_indices[DDPROF_PWT_LENGTH]
                                   [kNbEventAggregationModes];
  ddog_prof_ValueType perf_value_type[k_max_value_types] = {};
  int num_sample_type_ids = 0;

public:
  ProfValueTypes() {
    for (auto &indices : watcher_type_to_pprof_indices) {
      for (auto &index : indices) {
        index = -1;
      }
    }
  }

  void set_index(int watcher_type, EventAggregationModePos pos, int value) {
    watcher_type_to_pprof_indices[watcher_type][pos] = value;
  }

  void add_value_type(const char *name, const char *unit, int watcher_type,
                      EventAggregationModePos pos) {
    perf_value_type[num_sample_type_ids].type_ = to_CharSlice(name);
    perf_value_type[num_sample_type_ids].unit = to_CharSlice(unit);
    set_index(watcher_type, pos, num_sample_type_ids);
    ++num_sample_type_ids;
  }

  [[nodiscard]] int get_index(int watcher_type,
                              EventAggregationModePos pos) const {
    return watcher_type_to_pprof_indices[watcher_type][pos];
  }

  [[nodiscard]] int get_num_sample_type_ids() const {
    return num_sample_type_ids;
  }

  [[nodiscard]] ddog_prof_Slice_ValueType get_sample_types_slice() const {
    return {.ptr = perf_value_type,
            .len = static_cast<uintptr_t>(num_sample_type_ids)};
  }
};

ProfValueTypes compute_pprof_values(const ActiveIdsResult &active_ids) {
  ProfValueTypes result{};
  for (int i = 0; i < DDPROF_PWT_LENGTH; ++i) {
    if (active_ids.output_mode[i] == EventAggregationMode::kDisabled) {
      continue;
    }
    assert(i != DDPROF_PWT_NOCOUNT);
    for (int value_pos = 0; value_pos < kNbEventAggregationModes; ++value_pos) {
      if (Any(active_ids.output_mode[i] &
              static_cast<EventAggregationMode>(1 << value_pos))) {
        const char *value_name = sample_type_name_from_idx(
            i, static_cast<EventAggregationModePos>(value_pos));
        const char *value_unit = sample_type_unit_from_idx(i);
        if (!value_name || !value_unit) {
          LG_WRN("Malformed sample type (%d), ignoring", i);
          continue;
        }
        result.add_value_type(value_name, value_unit, i,
                              static_cast<EventAggregationModePos>(value_pos));
      }
    }
  }
  return result;
}

size_t prepare_labels(const UnwindOutput &uw_output, const PerfWatcher &watcher,
                      const ddog_prof_ProfilesDictionary *dict,
                      std::unordered_map<pid_t, std::string> &pid_strs,
                      std::span<ddog_prof_Label2> labels) {
  const LabelKeyIds &label_ids = get_label_key_ids(dict);
  size_t labels_num = 0;
  if (!uw_output.container_id.empty()) {
    labels[labels_num].key = label_ids.container_id;
    labels[labels_num].str = to_CharSlice(uw_output.container_id);
    ++labels_num;
  }

  // Add any configured labels.  Note that TID alone has the same cardinality as
  // (TID;PID) tuples, so except for symbol table overhead it doesn't matter
  // much if TID implies PID for clarity.
  if (!watcher.suppress_pid || !watcher.suppress_tid) {
    labels[labels_num].key = label_ids.process_id;
    labels[labels_num].str = to_CharSlice(pid_str(uw_output.pid, pid_strs));
    ++labels_num;
  }
  if (!watcher.suppress_tid) {
    labels[labels_num].key = label_ids.thread_id;
    labels[labels_num].str = to_CharSlice(pid_str(uw_output.tid, pid_strs));
    ++labels_num;
  }
  if (watcher_has_tracepoint(&watcher)) {
    labels[labels_num].key = label_ids.tracepoint_type;
    // If the label is given, use that as the tracepoint type. Otherwise,
    // default to the event name
    if (!watcher.tracepoint_label.empty()) {
      labels[labels_num].str = to_CharSlice(watcher.tracepoint_label);
    } else {
      labels[labels_num].str = to_CharSlice(watcher.tracepoint_event);
    }
    ++labels_num;
  }
  if (!uw_output.exe_name.empty()) {
    labels[labels_num].key = label_ids.process_name;
    labels[labels_num].str = to_CharSlice(uw_output.exe_name);
    ++labels_num;
  }
  if (!uw_output.thread_name.empty()) {
    labels[labels_num].key = label_ids.thread_name;
    labels[labels_num].str = to_CharSlice(uw_output.thread_name);
    ++labels_num;
  }
  DDPROF_DCHECK_FATAL(labels_num <= labels.size(),
                      "pprof_aggregate - label buffer exceeded");
  return labels_num;
}

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
    std::array<ddog_prof_Location, kMaxStackDepth> &locations_buff,
    std::array<ddog_prof_Location2, kMaxStackDepth> &locations2_buff,
    Symbolizer::BlazeResultsWrapper &session_results, unsigned &write_index) {
  unsigned index = 0;

  const ddprof::SymbolTable &symbol_table = symbol_hdr._symbol_table;
  const ddprof::MapInfoTable &mapinfo_table = symbol_hdr._mapinfo_table;

  // The -1 on size is because the last frame is binary.
  // We wait for the incomplete frame to be added if needed.
  // By removing the incomplete frame and pushing logic to BE
  // we can simplify this loop
  while (index < locs.size() - 1 && write_index < locations_buff.size()) {
    if (locs[index].symbol_idx != k_symbol_idx_null) {
      // Location already symbolized
      const FunLoc &loc = locs[index];
      write_location(loc, mapinfo_table[loc.map_info_idx],
                     symbol_table[loc.symbol_idx],
                     symbol_hdr.profiles_dictionary(),
                     &locations_buff[write_index]);
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
      if (locs[index].symbol_idx != k_symbol_idx_null) {
        break; // Stop if we find a symbolized location
      }
    }
    // Perform symbolization for all collected addresses
    const unsigned start_write_index = write_index;
    const DDRes res = symbolizer->symbolize_pprof(
        elf_addresses, file_id, current_file_path,
        mapinfo_table[locs[start_index].map_info_idx],
        std::span<ddog_prof_Location>{locations_buff}, write_index,
        session_results);
    for (unsigned i = start_write_index; i < write_index; ++i) {
      DDRES_CHECK_FWD(
          write_location2_from_location(dict, locations_buff[i],
                                        &locations2_buff[i]));
    }
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
          std::span<ddog_prof_Location>{locations_buff.data(), write_index})) {
    // Write a common frame to indicate an incomplete stack
    write_location(0, k_common_frame_names[incomplete_stack], {}, 0, {},
                   &locations_buff[write_index]);
    DDRES_CHECK_FWD(
        write_location2_from_location(dict, locations_buff[write_index],
                                      &locations2_buff[write_index]));
    ++write_index;
  }

  // Write the binary frame if it exists and is valid
  if (write_index < kMaxStackDepth &&
      locs.back().symbol_idx != k_symbol_idx_null) {
    const FunLoc &loc = locs.back();
    write_location(loc, mapinfo_table[loc.map_info_idx],
                   symbol_table[loc.symbol_idx],
                   symbol_hdr.profiles_dictionary(),
                   &locations_buff[write_index]);
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
  size_t const num_watchers = ctx.watchers.size();

  ActiveIdsResult active_ids = {};
  DDRES_CHECK_FWD(get_active_ids(std::span(ctx.watchers), active_ids));
#ifdef DEBUG
  LG_DBG("Active IDs :");
  for (int i = 0; i < DDPROF_PWT_LENGTH; ++i) {
    LG_DBG("%s --> %d ", sample_type_name_from_idx(i, kOccurrencePos),
           static_cast<int>(active_ids.output_mode[i]));
  }
#endif
  // Based on active IDs, prepare the list pf pprof values
  // pprof_values should stay alive while we create the pprof
  const ProfValueTypes pprof_values = compute_pprof_values(active_ids);

  // Update each watcher with matching types
  for (unsigned i = 0; i < num_watchers; ++i) {
    int const this_id = ctx.watchers[i].sample_type_id;
    if (this_id < 0 || this_id == DDPROF_PWT_NOCOUNT ||
        this_id >= DDPROF_PWT_LENGTH) {
      continue;
    }
    for (int value_pos = 0; value_pos < kNbEventAggregationModes; ++value_pos) {
      ctx.watchers[i].pprof_indices[value_pos].pprof_index =
          pprof_values.get_index(
              ctx.watchers[i].sample_type_id,
              static_cast<EventAggregationModePos>(value_pos));
      const int count_id = sample_type_id_to_count_sample_type_id(
          ctx.watchers[i].sample_type_id);
      if (count_id != DDPROF_PWT_NOCOUNT) {
        ctx.watchers[i].pprof_indices[value_pos].pprof_count_index =
            pprof_values.get_index(
                count_id, static_cast<EventAggregationModePos>(value_pos));
      }
    }
  }

  pprof->_nb_values = pprof_values.get_num_sample_type_ids();
  const ddog_prof_Slice_ValueType sample_types =
      pprof_values.get_sample_types_slice();
  ddog_prof_Period period;
  if (pprof->_nb_values > 0) {
    if (!active_ids.default_watcher) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "Unable to find default watcher");
    }
    // Populate the default.  If we have a frequency, assume it is given in
    // hertz and convert to a period in nanoseconds.  This is broken for many
    // event-based types (but providing frequency would also be broken in those
    // cases)
    int64_t default_period = active_ids.default_watcher->sample_period;
    if (active_ids.default_watcher->options.is_freq) {
      // convert to period (nano seconds)
      default_period =
          std::chrono::nanoseconds(std::chrono::seconds{1}).count() /
          default_period;
    }
    int default_index = -1;
    int value_pos = 0;
    while (default_index == -1 &&
           value_pos < EventAggregationModePos::kNbEventAggregationModes) {
      default_index =
          active_ids.default_watcher->pprof_indices[value_pos].pprof_index;
      ++value_pos;
    }
    if (default_index == -1) {
      DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF,
                             "Unable to find default watcher's value");
    }
    // period is the default watcher's type.
    period = {
        .type_ = sample_types.ptr[default_index],
        .value = default_period,
    };
  }

  ddog_prof_Status status = ddog_prof_Profile_with_dictionary(
      &pprof->_profile, dict, sample_types,
      pprof_values.get_num_sample_type_ids() > 0 ? &period : nullptr);

  if (status.err != nullptr) {
    defer { ddog_prof_Status_drop(&status); };
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "Unable to create new profile: %s",
                           status.err);
  }

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
  return {};
}

// Assumption of API is that sample is valid in a single type
DDRes pprof_aggregate(const UnwindOutput *uw_output,
                      const SymbolHdr &symbol_hdr, const DDProfValuePack &pack,
                      const PerfWatcher *watcher,
                      const FileInfoVector &file_infos, bool show_samples,
                      EventAggregationModePos value_pos, Symbolizer *symbolizer,
                      DDProfPProf *pprof) {

  const PProfIndices &pprof_indices = watcher->pprof_indices[value_pos];
  ddog_prof_Profile *profile = &pprof->_profile;
  int64_t values[k_max_value_types] = {};
  assert(pprof_indices.pprof_index != -1);
  values[pprof_indices.pprof_index] = pack.value;
  if (watcher_has_countable_sample_type(watcher)) {
    assert(pprof_indices.pprof_count_index != -1);
    values[pprof_indices.pprof_count_index] = pack.count;
  }

  std::array<ddog_prof_Location, kMaxStackDepth> locations_buff;
  std::array<ddog_prof_Location2, kMaxStackDepth> locations2_buff;
  std::span locs{uw_output->locs};
  locs = adjust_locations(watcher, locs);

  // Blaze results should remain alive until we aggregate the pprof data
  Symbolizer::BlazeResultsWrapper session_results;
  unsigned write_index = 0;
  DDRES_CHECK_FWD(process_symbolization(
      locs, symbol_hdr, file_infos, symbolizer,
      symbol_hdr.profiles_dictionary(), locations_buff, locations2_buff,
      session_results, write_index));
  std::array<ddog_prof_Label2, k_max_pprof_labels> labels{};
  // Create the labels for the sample.  Two samples are the same only when
  // their locations _and_ all labels are identical, so we admit a very limited
  // number of labels at present
  const size_t labels_num = prepare_labels(
      *uw_output, *watcher, symbol_hdr.profiles_dictionary(), pprof->_pid_str,
      std::span{labels});

  ddog_prof_Sample2 const sample = {
      .locations = {.ptr = locations2_buff.data(), .len = write_index},
      .values = {.ptr = values, .len = pprof->_nb_values},
      .labels = {.ptr = labels.data(), .len = labels_num},
  };

  if (show_samples) {
    ddprof_print_sample(std::span{locations_buff.data(), write_index},
                        pack.value, uw_output->pid, uw_output->tid, value_pos,
                        *watcher);
  }
  auto res = ddog_prof_Profile_add2(profile, sample, pack.timestamp);
  if (res.err != nullptr) {
    defer { ddog_prof_Status_drop(&res); };
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "Unable to add profile: %s",
                           res.err);
  }
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
  if (watcher_has_countable_sample_type(watcher)) {
    assert(pprof_indices.pprof_count_index != -1);
    values[pprof_indices.pprof_count_index] = pack.count;
  }

  std::array<ddog_prof_Location, kMaxStackDepth> locations_buff;
  std::array<ddog_prof_Location2, kMaxStackDepth> locations2_buff;
  std::span locs{uw_output->locs};
  locs = adjust_locations(watcher, locs);

  Symbolizer::BlazeResultsWrapper session_results;
  unsigned write_index = 0;
  DDRES_CHECK_FWD(process_symbolization(
      locs, symbol_hdr, file_infos, symbolizer,
      symbol_hdr.profiles_dictionary(), locations_buff, locations2_buff,
      session_results, write_index));

  ddog_prof_LabelSetId labelset_id{};
  DDRES_CHECK_FWD(intern_profile_labelset(profile, *uw_output, *watcher,
                                          pprof->_pid_str, &labelset_id));

  std::array<ddog_prof_LocationId, kMaxStackDepth> location_ids{};
  for (unsigned i = 0; i < write_index; ++i) {
    DDRES_CHECK_FWD(
        intern_profile_location_id(profile, locations_buff[i],
                                   &location_ids[i]));
  }
  ddog_prof_StackTraceId_Result stacktrace_res =
      ddog_prof_Profile_intern_stacktrace(
          profile, {.ptr = location_ids.data(), .len = write_index});
  if (stacktrace_res.tag !=
      DDOG_PROF_STACK_TRACE_ID_RESULT_OK_GENERATIONAL_ID_STACK_TRACE_ID) {
    ddog_Error_drop(&stacktrace_res.err);
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "Unable to intern stacktrace");
  }

  if (show_samples) {
    ddprof_print_sample(std::span{locations_buff.data(), write_index},
                        pack.value, uw_output->pid, uw_output->tid, value_pos,
                        *watcher);
  }

  ddog_VoidResult res = ddog_prof_Profile_intern_sample(
      profile, stacktrace_res.ok, {.ptr = values, .len = pprof->_nb_values},
      labelset_id, pack.timestamp);
  if (res.tag != DDOG_VOID_RESULT_OK) {
    ddog_Error_drop(&res.err);
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "Unable to add profile");
  }
  return {};
}
} // namespace ddprof
