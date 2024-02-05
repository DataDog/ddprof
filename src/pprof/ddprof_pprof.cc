// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "pprof/ddprof_pprof.hpp"

#include "ddog_profiling_utils.hpp"
#include "ddprof_defs.hpp"
#include "ddres.hpp"
#include "defer.hpp"
#include "pevent_lib.hpp"
#include "symbol_hdr.hpp"

#include <absl/strings/str_format.h>
#include <absl/strings/substitute.h>
#include <cstdio>
#include <cstring>
#include <span>

namespace ddprof {

namespace {
constexpr size_t k_max_pprof_labels{6};

void write_function(const Symbol &symbol, ddog_prof_Function *ffi_func) {
  ffi_func->name = to_CharSlice(symbol._demangle_name);
  ffi_func->system_name = to_CharSlice(symbol._symname);
  ffi_func->filename = to_CharSlice(symbol._srcpath);
  // Not filed (can be computed if needed using the start range from elf)
  ffi_func->start_line = 0;
}

void write_mapping(const MapInfo &mapinfo, ddog_prof_Mapping *ffi_mapping) {
  ffi_mapping->memory_start = mapinfo._low_addr;
  ffi_mapping->memory_limit = mapinfo._high_addr;
  ffi_mapping->file_offset = mapinfo._offset;
  ffi_mapping->filename = to_CharSlice(mapinfo._sopath);
  ffi_mapping->build_id = to_CharSlice(mapinfo._build_id);
}

void write_location(const FunLoc *loc, const MapInfo &mapinfo,
                    const Symbol &symbol, ddog_prof_Location *ffi_location,
                    bool use_process_adresses) {
  write_mapping(mapinfo, &ffi_location->mapping);
  write_function(symbol, &ffi_location->function);
  ffi_location->address = use_process_adresses ? loc->ip : loc->elf_addr;
  ffi_location->line = symbol._lineno;
}

constexpr int k_max_value_types =
    DDPROF_PWT_LENGTH * static_cast<int>(kNbEventAggregationModes);

struct ActiveIdsResult {
  EventAggregationMode output_mode[DDPROF_PWT_LENGTH] = {};
  PerfWatcher *default_watcher = nullptr;
};

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
} // namespace

DDRes pprof_create_profile(DDProfPProf *pprof, DDProfContext &ctx) {
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
  auto prof_res = ddog_prof_Profile_new(
      sample_types,
      pprof_values.get_num_sample_type_ids() > 0 ? &period : nullptr, nullptr);

  if (prof_res.tag != DDOG_PROF_PROFILE_NEW_RESULT_OK) {
    ddog_Error_drop(&prof_res.err);
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "Unable to create new profile");
  }
  pprof->_profile = prof_res.ok;

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
    pprof->use_process_adresses = false;
  } else {
    pprof->use_process_adresses = true;
  }

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
                      EventAggregationModePos value_pos, DDProfPProf *pprof) {

  const ddprof::SymbolTable &symbol_table = symbol_hdr._symbol_table;
  const ddprof::MapInfoTable &mapinfo_table = symbol_hdr._mapinfo_table;

  ddog_prof_Profile *profile = &pprof->_profile;
  const PProfIndices &pprof_indices = watcher->pprof_indices[value_pos];
  int64_t values[k_max_value_types] = {};
  assert(pprof_indices.pprof_index != -1);
  values[pprof_indices.pprof_index] = pack.value;
  if (watcher_has_countable_sample_type(watcher)) {
    assert(pprof_indices.pprof_count_index != -1);
    values[pprof_indices.pprof_count_index] = pack.count;
  }

  ddog_prof_Location locations_buff[kMaxStackDepth];
  std::span locs{uw_output->locs};

  if (watcher->options.nb_frames_to_skip < locs.size()) {
    locs = locs.subspan(watcher->options.nb_frames_to_skip);
  } else {
    // Keep the last two frames. In the case of stacks that we could not unwind
    // We will have the following stack: binary_name; [incomplete]
    if (locs.size() >= 3) {
      locs = locs.subspan(locs.size() - 2);
    }
  }

  unsigned cur_loc = 0;
  for (const FunLoc &loc : locs) {
    // possibly several lines to handle inlined function (not handled for now)
    write_location(&loc, mapinfo_table[loc._map_info_idx],
                   symbol_table[loc._symbol_idx], &locations_buff[cur_loc],
                   pprof->use_process_adresses);
    ++cur_loc;
  }

  // Create the labels for the sample.  Two samples are the same only when
  // their locations _and_ all labels are identical, so we admit a very limited
  // number of labels at present
  ddog_prof_Label labels[k_max_pprof_labels] = {};
  size_t labels_num = 0;
  char pid_str[sizeof("536870912")] = {}; // reserve space up to 2^29 base-10
  char tid_str[sizeof("536870912")] = {}; // reserve space up to 2^29 base-10

  labels[labels_num].key = to_CharSlice("container_id");
  labels[labels_num].str = to_CharSlice(uw_output->container_id);
  ++labels_num;

  // Add any configured labels.  Note that TID alone has the same cardinality as
  // (TID;PID) tuples, so except for symbol table overhead it doesn't matter
  // much if TID implies PID for clarity.
  if (!watcher->suppress_pid || !watcher->suppress_tid) {
    snprintf(pid_str, sizeof(pid_str), "%d", uw_output->pid);
    labels[labels_num].key = to_CharSlice("process_id");
    labels[labels_num].str = to_CharSlice(pid_str);
    ++labels_num;
  }
  if (!watcher->suppress_tid) {
    snprintf(tid_str, sizeof(tid_str), "%d", uw_output->tid);
    // This naming has an impact on backend side (hence the inconsistency with
    // process_id)
    labels[labels_num].key = to_CharSlice("thread id");
    labels[labels_num].str = to_CharSlice(tid_str);
    ++labels_num;
  }
  if (watcher_has_tracepoint(watcher)) {
    labels[labels_num].key = to_CharSlice("tracepoint_type");

    // If the label is given, use that as the tracepoint type. Otherwise,
    // default to the event name
    if (!watcher->tracepoint_label.empty()) {
      labels[labels_num].str = to_CharSlice(watcher->tracepoint_label);
    } else {
      labels[labels_num].str = to_CharSlice(watcher->tracepoint_event);
    }
    ++labels_num;
  }
  assert(labels_num <= k_max_pprof_labels);

  ddog_prof_Sample const sample = {
      .locations = {.ptr = locations_buff, .len = cur_loc},
      .values = {.ptr = values, .len = pprof->_nb_values},
      .labels = {.ptr = labels, .len = labels_num},
  };

  auto res = ddog_prof_Profile_add(profile, sample, pack.timestamp);
  if (res.tag != DDOG_PROF_PROFILE_RESULT_OK) {
    defer { ddog_Error_drop(&res.err); };
    auto msg = ddog_Error_message(&res.err);
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "Unable to add profile: %*s",
                           static_cast<int>(msg.len), msg.ptr);
  }
  return {};
}

DDRes pprof_reset(DDProfPProf *pprof) {
  auto res = ddog_prof_Profile_reset(&pprof->_profile, nullptr);
  if (res.tag != DDOG_PROF_PROFILE_RESULT_OK) {
    defer { ddog_Error_drop(&res.err); };
    auto msg = ddog_Error_message(&res.err);
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "Unable to reset profile: %*s",
                           static_cast<int>(msg.len), msg.ptr);
  }
  return {};
}

void ddprof_print_sample(const UnwindOutput &uw_output,
                         const SymbolHdr &symbol_hdr, uint64_t value,
                         EventAggregationModePos value_mode_pos,
                         const PerfWatcher &watcher) {

  const auto &symbol_table = symbol_hdr._symbol_table;
  std::span const locs{uw_output.locs};

  const char *sample_name =
      sample_type_name_from_idx(watcher.sample_type_id, value_mode_pos);
  std::string buf = absl::Substitute("sample[type=$0;pid=$1;tid=$2] ",
                                     sample_name, uw_output.pid, uw_output.tid);

  for (auto loc_it = locs.rbegin(); loc_it != locs.rend(); ++loc_it) {
    const auto &sym = symbol_table[loc_it->_symbol_idx];
    if (loc_it != locs.rbegin()) {
      buf += ";";
    }
    if (sym._symname.empty()) {
      if (loc_it->ip == 0) {
        std::string_view const path{sym._srcpath};
        auto pos = path.rfind('/');
        buf += "(";
        buf += path.substr(pos == std::string_view::npos ? 0 : pos + 1);
        buf += ")";
      } else {
        absl::StrAppendFormat(&buf, "%#x/%#x", loc_it->ip, loc_it->elf_addr);
      }
    } else {
      std::string_view const func{sym._symname};
      buf += func.substr(0, func.find('('));
    }
  }

  PRINT_NFO("%s %ld", buf.c_str(), value);
}
} // namespace ddprof
