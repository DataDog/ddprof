// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "pprof/ddprof_pprof.hpp"

#include "ddog_profiling_utils.hpp"
#include "ddprof_defs.hpp"
#include "ddres.hpp"
#include "pevent_lib.hpp"
#include "span.hpp"
#include "string_format.hpp"
#include "symbol_hdr.hpp"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PPROF_MAX_LABELS 5

DDRes pprof_create_profile(DDProfPProf *pprof, DDProfContext *ctx) {
  PerfWatcher *watchers = ctx->watchers;
  size_t num_watchers = ctx->num_watchers;
  ddog_ValueType perf_value_type[DDPROF_PWT_LENGTH];

  // Figure out which sample_type_ids are used by active watchers
  // We also record the watcher with the lowest valid sample_type id, since that
  // will serve as the default for the pprof
  bool active_ids[DDPROF_PWT_LENGTH] = {};
  PerfWatcher *default_watcher = watchers;
  for (unsigned i = 0; i < num_watchers; ++i) {
    int this_id = watchers[i].sample_type_id;
    int count_id = sample_type_id_to_count_sample_type_id(this_id);
    if (this_id < 0 || this_id == DDPROF_PWT_NOCOUNT ||
        this_id >= DDPROF_PWT_LENGTH) {
      if (this_id != DDPROF_PWT_NOCOUNT) {
        DDRES_RETURN_ERROR_LOG(
            DD_WHAT_PPROF, "Watcher \"%s\" (%d) has invalid sample_type_id %d",
            watchers[i].desc.c_str(), i, this_id);
      }
      continue;
    }

    if (this_id <= default_watcher->sample_type_id) // update default
      default_watcher = &watchers[i];
    active_ids[this_id] = true; // update mask
    if (count_id != DDPROF_PWT_NOCOUNT)
      active_ids[count_id] = true; // if the count is valid, update mask for it
  }

  // Convert the mask into a lookup.  While we're at it, populate the metadata
  // for the pprof
  int pv[DDPROF_PWT_LENGTH] = {};
  int num_sample_type_ids = 0;
  for (int i = 0; i < DDPROF_PWT_LENGTH; ++i) {
    if (!active_ids[i])
      continue;
    assert(i != DDPROF_PWT_NOCOUNT);

    const char *value_name = sample_type_name_from_idx(i);
    const char *value_unit = sample_type_unit_from_idx(i);
    if (!value_name || !value_unit) {
      LG_WRN("Malformed sample type (%d), ignoring", i);
      continue;
    }
    perf_value_type[num_sample_type_ids].type_ = to_CharSlice(value_name);
    perf_value_type[num_sample_type_ids].unit = to_CharSlice(value_unit);

    // Update the pv
    pv[i] = num_sample_type_ids;
    ++num_sample_type_ids;
  }

  // Update each watcher
  for (unsigned i = 0; i < num_watchers; ++i) {
    int this_id = watchers[i].sample_type_id;
    if (this_id < 0 || this_id == DDPROF_PWT_NOCOUNT ||
        this_id >= DDPROF_PWT_LENGTH) {
      continue;
    }
    int permuted_id = pv[watchers[i].sample_type_id];
    int permuted_count_id = pv[watcher_to_count_sample_type_id(&watchers[i])];

    watchers[i].pprof_sample_idx = permuted_id;
    if (watcher_has_countable_sample_type(&watchers[i])) {
      watchers[i].pprof_count_sample_idx = permuted_count_id;
    }
  }

  pprof->_nb_values = num_sample_type_ids;
  ddog_Slice_value_type sample_types = {.ptr = perf_value_type,
                                        .len = pprof->_nb_values};

  ddog_Period period;
  if (num_sample_type_ids > 0) {
    // Populate the default.  If we have a frequency, assume it is given in
    // hertz and convert to a period in nanoseconds.  This is broken for many
    // event- based types (but providing frequency would also be broken in those
    // cases)
    int64_t default_period = default_watcher->sample_period;
    if (default_watcher->options.is_freq)
      default_period = 1e9 / default_period;

    period = {
        .type_ = perf_value_type[pv[default_watcher->pprof_sample_idx]],
        .value = default_period,
    };
  }
  pprof->_profile = ddog_Profile_new(
      sample_types, num_sample_type_ids > 0 ? &period : nullptr, nullptr);
  if (!pprof->_profile) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "Unable to create profile");
  }

  // Add relevant tags
  {
    bool include_kernel =
        pevent_include_kernel_events(&ctx->worker_ctx.pevent_hdr);
    pprof->_tags.push_back(std::make_pair(
        std::string("include_kernel"),
        include_kernel ? std::string("true") : std::string("false")));
  }

  return ddres_init();
}

DDRes pprof_free_profile(DDProfPProf *pprof) {
  if (pprof->_profile) {
    ddog_Profile_free(pprof->_profile);
  }
  pprof->_profile = NULL;
  pprof->_nb_values = 0;
  return ddres_init();
}

static void write_function(const ddprof::Symbol &symbol,
                           ddog_Function *ffi_func) {
  ffi_func->name = to_CharSlice(symbol._demangle_name);
  ffi_func->system_name = to_CharSlice(symbol._symname);
  ffi_func->filename = to_CharSlice(symbol._srcpath);
  // Not filed (can be computed if needed using the start range from elf)
  ffi_func->start_line = 0;
}

static void write_mapping(const ddprof::MapInfo &mapinfo,
                          ddog_Mapping *ffi_mapping) {
  ffi_mapping->memory_start = mapinfo._low_addr;
  ffi_mapping->memory_limit = mapinfo._high_addr;
  ffi_mapping->file_offset = mapinfo._offset;
  ffi_mapping->filename = to_CharSlice(mapinfo._sopath);
  ffi_mapping->build_id = {};
}

static void write_location(const FunLoc *loc, const ddprof::MapInfo &mapinfo,
                           const ddog_Slice_line *lines,
                           ddog_Location *ffi_location) {
  write_mapping(mapinfo, &ffi_location->mapping);
  ffi_location->address = loc->ip;
  ffi_location->lines = *lines;
  // Folded not handled for now
  ffi_location->is_folded = false;
}

static void write_line(const ddprof::Symbol &symbol, ddog_Line *ffi_line) {
  write_function(symbol, &ffi_line->function);
  ffi_line->line = symbol._lineno;
}

// Assumption of API is that sample is valid in a single type
DDRes pprof_aggregate(const UnwindOutput *uw_output,
                      const SymbolHdr *symbol_hdr, uint64_t value,
                      uint64_t count, const PerfWatcher *watcher,
                      DDProfPProf *pprof) {

  const ddprof::SymbolTable &symbol_table = symbol_hdr->_symbol_table;
  const ddprof::MapInfoTable &mapinfo_table = symbol_hdr->_mapinfo_table;
  ddog_Profile *profile = pprof->_profile;

  int64_t values[DDPROF_PWT_LENGTH] = {};
  values[watcher->pprof_sample_idx] = value * count;
  if (watcher_has_countable_sample_type(watcher)) {
    values[watcher->pprof_count_sample_idx] = count;
  }

  ddog_Location locations_buff[DD_MAX_STACK_DEPTH];
  // assumption of single line per loc for now
  ddog_Line line_buff[DD_MAX_STACK_DEPTH];

  ddprof::span locs{uw_output->locs, uw_output->nb_locs};

  if (watcher->options.nb_frames_to_skip < locs.size()) {
    locs = locs.subspan(watcher->options.nb_frames_to_skip);
  }

  unsigned cur_loc = 0;
  for (const FunLoc &loc : locs) {
    // possibly several lines to handle inlined function (not handled for now)
    write_line(symbol_table[loc._symbol_idx], &line_buff[cur_loc]);
    ddog_Slice_line lines = {.ptr = &line_buff[cur_loc], .len = 1};
    write_location(&loc, mapinfo_table[loc._map_info_idx], &lines,
                   &locations_buff[cur_loc]);
    ++cur_loc;
  }

  // Create the labels for the sample.  Two samples are the same only when
  // their locations _and_ all labels are identical, so we admit a very limited
  // number of labels at present
  ddog_Label labels[PPROF_MAX_LABELS] = {};
  size_t labels_num = 0;
  char pid_str[sizeof("536870912")] = {}; // reserve space up to 2^29 base-10
  char tid_str[sizeof("536870912")] = {}; // reserve space up to 2^29 base-10

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

    // If the label is given, use that as the tracepoint type.  Otherwise
    // default to the event name
    if (!watcher->tracepoint_label.empty()) {
      labels[labels_num].str = to_CharSlice(watcher->tracepoint_label.c_str());
    } else {
      labels[labels_num].str = to_CharSlice(watcher->tracepoint_event.c_str());
    }
    ++labels_num;
  }
  ddog_Sample sample = {
      .locations = {.ptr = locations_buff, .len = cur_loc},
      .values = {.ptr = values, .len = pprof->_nb_values},
      .labels = {.ptr = labels, .len = labels_num},
  };

  uint64_t id_sample = ddog_Profile_add(profile, sample);
  if (id_sample == 0) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "Unable to add profile");
  }

  return ddres_init();
}

DDRes pprof_reset(DDProfPProf *pprof) {
  if (!ddog_Profile_reset(pprof->_profile, nullptr)) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "Unable to reset profile");
  }
  return ddres_init();
}

void ddprof_print_sample(const UnwindOutput &uw_output,
                         const SymbolHdr &symbol_hdr, uint64_t value,
                         const PerfWatcher &watcher) {

  auto &symbol_table = symbol_hdr._symbol_table;
  ddprof::span locs{uw_output.locs, uw_output.nb_locs};

  const char *sample_name = sample_type_name_from_idx(
      sample_type_id_to_count_sample_type_id(watcher.sample_type_id));

  std::string buf =
      ddprof::string_format("sample[type=%s;pid=%ld;tid=%ld] ", sample_name,
                            uw_output.pid, uw_output.tid);

  for (auto loc_it = locs.rbegin(); loc_it != locs.rend(); ++loc_it) {
    auto &sym = symbol_table[loc_it->_symbol_idx];
    if (loc_it != locs.rbegin()) {
      buf += ";";
    }
    if (sym._symname.empty()) {
      if (loc_it->ip == 0) {
        std::string_view path{sym._srcpath};
        auto pos = path.rfind('/');
        buf += "(";
        buf += path.substr(pos == std::string_view::npos ? 0 : pos + 1);
        buf += ")";
      } else {
        buf += ddprof::string_format("%p", loc_it->ip);
      }
    } else {
      std::string_view func{sym._symname};
      buf += func.substr(0, func.find('('));
    }
  }

  PRINT_NFO("%s %ld", buf.c_str(), value);
}
