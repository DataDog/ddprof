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

DDRes pprof_create_profile(DDProfPProf *pprof, unsigned type_default,
    int64_t periodfreq_default) {
  // Add all the defined profile types (perf_watcher.h)
  struct ddprof_ffi_ValueType perf_value_type[DDPROF_PWT_LENGTH];
  for (unsigned i = 0; i < DDPROF_PWT_LENGTH; ++i) {
    const char *value_name = profile_name_from_idx(i);
    const char *value_unit = profile_unit_from_idx(i);
    if (!value_name || !value_unit) {
      LG_WRN("Malformed sample type (%d)", i);
      continue;
    }
    perf_value_type[i].type_ = (ddprof_ffi_Slice_c_char) {
      .ptr = value_name,
      .len = strlen(value_name)};
    perf_value_type[i].unit = (ddprof_ffi_Slice_c_char) {
      .ptr = value_unit,
      .len = strlen(value_unit)};
  }

  pprof->_nb_values = DDPROF_PWT_LENGTH;
  struct ddprof_ffi_Slice_value_type sample_types = {.ptr = perf_value_type,
                                                     .len = pprof->_nb_values};
  struct ddprof_ffi_Period period = {
    .type_ = perf_value_type[type_default],
    .value = periodfreq_default
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
  values[watcher->profile_id] = value;
  if (is_countable_type(watcher->profile_id))
    values[watcher->count_id] = 1;

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
  if (watcher->type == PERF_TYPE_TRACEPOINT) {
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
