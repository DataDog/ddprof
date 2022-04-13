// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

extern "C" {
#include "pprof/ddprof_pprof.h"

#include "ddres.h"

#include <ddprof/ffi.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ddprof_defs.h"
}

#include "symbol_hdr.hpp"

// Slice helpers
static ddprof_ffi_Slice_c_char
std_string_2_slice_c_char(const std::string &str) {
  if (str.empty())
    return (struct ddprof_ffi_Slice_c_char){.ptr = NULL, .len = 0};
  else
    return (struct ddprof_ffi_Slice_c_char){.ptr = str.c_str(),
                                            .len = str.length()};
}

#define SLICE_LITERAL(str)                                                     \
  (struct ddprof_ffi_Slice_c_char) { .ptr = (str), .len = sizeof(str) - 1 }

#define PPROF_MAX_LABELS 5

static ddprof_ffi_Slice_c_char ffi_empty_char_slice(void) {
  return (struct ddprof_ffi_Slice_c_char){.ptr = (NULL), .len = 0};
}

DDRes pprof_create_profile(DDProfPProf *pprof, DDProfContext *ctx) {
  PerfWatcher *watchers = ctx->watchers;
  size_t num_watchers = ctx->num_watchers;
  struct ddprof_ffi_ValueType perf_value_type[DDPROF_PWT_LENGTH];

  // Figure out which sample_type_ids are used by active watchers
  // We also record the watcher with the lowest valid sample_type id, since that
  // will serve as the default for the pprof
  bool active_ids[DDPROF_PWT_LENGTH] = {};
  PerfWatcher *default_watcher = &watchers[0];
  for (unsigned i = 0; i < num_watchers; ++i) {
    int this_id = watchers[i].sample_type_id;
    int count_id = sample_type_id_to_count_sample_type_id(this_id);
    if (this_id < 0 || this_id == DDPROF_PWT_NOCOUNT || this_id >= DDPROF_PWT_LENGTH) {
      LG_WRN("Watcher \"%s\" (%d) has invalid sample_type_id %d, ignoring",
          watchers[i].desc, i, this_id);
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
  int pv[DDPROF_PWT_LENGTH];
  int num_sample_type_ids = 0;
  memset(pv, 0, sizeof(pv));
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
    perf_value_type[num_sample_type_ids].type_ = (ddprof_ffi_Slice_c_char) {
      .ptr = value_name,
      .len = strlen(value_name)};
    perf_value_type[num_sample_type_ids].unit = (ddprof_ffi_Slice_c_char) {
      .ptr = value_unit,
      .len = strlen(value_unit)};

    // Update the pv
    pv[i] = num_sample_type_ids;
    ++num_sample_type_ids;
  }

  // Update each watcher
  for (unsigned i = 0; i < num_watchers; ++i) {
    int permuted_id = pv[watchers[i].sample_type_id];
    int permuted_count_id = pv[watcher_to_count_sample_type_id(&watchers[i])];

    watchers[i].pprof_sample_idx = permuted_id;
    if (watcher_has_countable_sample_type(&watchers[i])) {
      watchers[i].pprof_count_sample_idx = permuted_count_id;
    }
  }

  // If none of the samples were good, that's an error
  if (!num_sample_type_ids) {
    // We use the phrase "profile type" in the error, since this is more
    // obvious for customers.
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "No valid profile types given");
  }

  pprof->_nb_values = num_sample_type_ids;
  struct ddprof_ffi_Slice_value_type sample_types = {.ptr = perf_value_type,
                                                     .len = pprof->_nb_values};

  // Populate the default.  If we have a frequency, assume it is given in hertz
  // and convert to a period in nanoseconds.  This is broken for many event-
  // based types (but providing frequency would also be broken in those cases)
  int64_t default_period = default_watcher->sample_period;
  if (default_watcher->options.is_freq)
    default_period = 1e9/default_period;

  struct ddprof_ffi_Period period = {
    .type_ = perf_value_type[pv[default_watcher->pprof_sample_idx]],
    .value = default_period,
  };

  pprof->_profile = ddprof_ffi_Profile_new(sample_types, &period);
  if (!pprof->_profile) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "Unable to allocate profiles");
  }
  return ddres_init();
}

DDRes pprof_free_profile(DDProfPProf *pprof) {
  ddprof_ffi_Profile_free(pprof->_profile);
  pprof->_profile = NULL;
  pprof->_nb_values = 0;
  return ddres_init();
}

static void write_function(const ddprof::Symbol &symbol,
                           ddprof_ffi_Function *ffi_func) {
  ffi_func->name = std_string_2_slice_c_char(symbol._demangle_name);
  ffi_func->system_name = std_string_2_slice_c_char(symbol._symname);
  ffi_func->filename = std_string_2_slice_c_char(symbol._srcpath);
  // Not filed (can be computed if needed using the start range from elf)
  ffi_func->start_line = 0;
}

#define UNKNOWN_BUILD_ID ffi_empty_char_slice()

static void write_mapping(const ddprof::MapInfo &mapinfo,
                          ddprof_ffi_Mapping *ffi_mapping) {
  ffi_mapping->memory_start = mapinfo._low_addr;
  ffi_mapping->memory_limit = mapinfo._high_addr;
  ffi_mapping->file_offset = mapinfo._offset;
  ffi_mapping->filename = std_string_2_slice_c_char(mapinfo._sopath);
  ffi_mapping->build_id = UNKNOWN_BUILD_ID;
}

static void write_location(const FunLoc *loc, const ddprof::MapInfo &mapinfo,
                           const ddprof_ffi_Slice_line *lines,
                           ddprof_ffi_Location *ffi_location) {
  write_mapping(mapinfo, &ffi_location->mapping);
  ffi_location->address = loc->ip;
  ffi_location->lines = *lines;
  // Folded not handled for now
  ffi_location->is_folded = false;
}

static void write_line(const ddprof::Symbol &symbol,
                       ddprof_ffi_Line *ffi_line) {
  write_function(symbol, &ffi_line->function);
  ffi_line->line = symbol._lineno;
}

// Assumption of API is that sample is valid in a single type
DDRes pprof_aggregate(const UnwindOutput *uw_output,
                      const SymbolHdr *symbol_hdr, uint64_t value,
                      const PerfWatcher *watcher, DDProfPProf *pprof) {

  const ddprof::SymbolTable &symbol_table = symbol_hdr->_symbol_table;
  const ddprof::MapInfoTable &mapinfo_table = symbol_hdr->_mapinfo_table;
  ddprof_ffi_Profile *profile = pprof->_profile;

  int64_t values[DDPROF_PWT_LENGTH] = {0};
  values[watcher->pprof_sample_idx] = value;
  if (watcher_has_countable_sample_type(watcher))
    values[watcher->pprof_count_sample_idx] = 1;

  ddprof_ffi_Location locations_buff[DD_MAX_STACK_DEPTH];
  // assumption of single line per loc for now
  ddprof_ffi_Line line_buff[DD_MAX_STACK_DEPTH];

  const FunLoc *locs = uw_output->locs;
  for (unsigned i = 0; i < uw_output->nb_locs; ++i) {
    // possibly several lines to handle inlined function (not handled for now)
    write_line(symbol_table[locs[i]._symbol_idx], &line_buff[i]);
    ddprof_ffi_Slice_line lines = {.ptr = &line_buff[i], .len = 1};
    write_location(&locs[i], mapinfo_table[locs[i]._map_info_idx], &lines,
                   &locations_buff[i]);
  }

  // Create the labels for the sample.  Two samples are the same only when
  // their locations _and_ all labels are identical, so we admit a very limited
  // number of labels at present
  struct ddprof_ffi_Label labels[PPROF_MAX_LABELS] = {};
  size_t labels_num = 0;

  // The max PID on Linux is approximately 2^22 (~4e6), which also limits TID
  char pid_str[7] = {};
  char tid_str[7] = {};

  // These labels are hardcoded on the Datadog backend
  static char pid_key[] = "process_id";
  static char tid_key[] = "thread_id";
  static char tracepoint_key[] = "tracepoint_type";

  // Add any configured labels.  Note that TID alone has the same cardinalit as
  // (TID;PID) tuples, so except for symbol table overhead it doesn't matter
  // much if TID implies PID for clarity.
  if (watcher->send_pid || watcher->send_tid) {
    size_t sz = snprintf(pid_str, sizeof(pid_str), "%d", uw_output->pid);
    labels[labels_num].key = (struct ddprof_ffi_Slice_c_char){.ptr = pid_key, .len = sizeof(pid_key)-1};
    labels[labels_num].str = (struct ddprof_ffi_Slice_c_char){.ptr = pid_str, .len = sz};
    ++labels_num;
  }
  if (watcher->send_tid) {
    size_t sz = snprintf(tid_str, sizeof(tid_str), "%d", uw_output->tid);
    labels[labels_num].key = (struct ddprof_ffi_Slice_c_char){.ptr = tid_key, .len = sizeof(tid_key)-1};
    labels[labels_num].str = (struct ddprof_ffi_Slice_c_char){.ptr = tid_str, .len = sz};
    ++labels_num;
  }
  if (watcher_has_tracepoint(watcher)) {
    // This adds only the trace name.  Maybe we should have group + tracenames?
    labels[labels_num].key = (struct ddprof_ffi_Slice_c_char){.ptr = tracepoint_key, .len = sizeof(tracepoint_key) - 1};
    labels[labels_num].str = (struct ddprof_ffi_Slice_c_char){.ptr = watcher->tracepoint_name, .len = strlen(watcher->tracepoint_name)};
    ++labels_num;
  }
  struct ddprof_ffi_Sample sample = {
      .locations = {.ptr = locations_buff, .len = uw_output->nb_locs},
      .values = {.ptr = values, .len = pprof->_nb_values},
      .labels = {.ptr = labels, .len = labels_num},
  };

  uint64_t id_sample = ddprof_ffi_Profile_add(profile, sample);
  if (id_sample == 0) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "Unable to add profile");
  }

  return ddres_init();
}

DDRes pprof_reset(DDProfPProf *pprof) {
  if (!ddprof_ffi_Profile_reset(pprof->_profile)) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "Unable to reset profile");
  }
  return ddres_init();
}
