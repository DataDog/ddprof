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

static const unsigned long s_nanos_in_one_sec = 1000000000;

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
  (struct ddprof_ffi_Slice_c_char) { .ptr = (str), sizeof(str) - 1 }

static ddprof_ffi_Slice_c_char ffi_empty_char_slice(void) {
  return (struct ddprof_ffi_Slice_c_char){.ptr = (NULL), 0};
}

static const struct ddprof_ffi_ValueType s_sample_count = {
    .type_ = SLICE_LITERAL("sample"),
    .unit = SLICE_LITERAL("count"),
};

static const struct ddprof_ffi_ValueType s_cpu_time = {
    .type_ = SLICE_LITERAL("cpu-time"),
    .unit = SLICE_LITERAL("nanoseconds"),
};

static const struct ddprof_ffi_ValueType s_cpu_sample = {
    .type_ = SLICE_LITERAL("cpu-sample"),
    .unit = SLICE_LITERAL("count"),
};

DDRes pprof_create_profile(DDProfPProf *pprof, const PerfOption *options,
                           unsigned nbOptions) {
  // Create one value
  struct ddprof_ffi_ValueType perf_value_type[MAX_TYPE_WATCHER + 1];
  unsigned value_index = 0;
  perf_value_type[value_index++] = s_sample_count;

  /* Add one value type per watcher */
  for (; value_index < nbOptions + 1; ++value_index) {
    ddprof_ffi_Slice_c_char perf_value_slice = {.ptr = options->label,
                                                .len = strlen(options->label)};
    ddprof_ffi_Slice_c_char perf_unit = {.ptr = options->unit,
                                         .len = strlen(options->unit)};

    perf_value_type[value_index].type_ = perf_value_slice;
    perf_value_type[value_index].unit = perf_unit;
  }

  pprof->_nb_values = value_index;
  struct ddprof_ffi_Slice_value_type sample_types = {.ptr = perf_value_type,
                                                     .len = value_index};
  struct ddprof_ffi_Period period;
  if (options->freq) {
    period.type_ = s_cpu_time;
    period.value = (s_nanos_in_one_sec) / options->sample_frequency;
  } else {
    period.type_ = s_cpu_sample;
    period.value = options->sample_period;
  }

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

static void write_location(const FunLoc *loc, const ddprof::Symbol &symbol,
                           const ddprof::MapInfo &mapinfo,
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
                      int watcher_idx, DDProfPProf *pprof) {

  const ddprof::SymbolTable &symbol_table = symbol_hdr->_symbol_table;
  const ddprof::MapInfoTable &mapinfo_table = symbol_hdr->_mapinfo_table;
  ddprof_ffi_Profile *profile = pprof->_profile;

  int64_t values[MAX_TYPE_WATCHER + 1] = {0};
  // Counted a single time
  values[0] = 1;
  // Add a value to the watcher we are sampling, leave others zeroed
  values[watcher_idx + 1] = value;

  ddprof_ffi_Location locations_buff[DD_MAX_STACK_DEPTH];
  // assumption of single line per loc for now
  ddprof_ffi_Line line_buff[DD_MAX_STACK_DEPTH];

  const FunLoc *locs = uw_output->locs;
  for (unsigned i = 0; i < uw_output->nb_locs; ++i) {
    // possibly several lines to handle inlined function (not handled for now)
    write_line(symbol_table[locs[i]._symbol_idx], &line_buff[i]);
    ddprof_ffi_Slice_line lines = {.ptr = &line_buff[i], .len = 1};
    write_location(&locs[i], symbol_table[locs[i]._symbol_idx],
                   mapinfo_table[locs[i]._map_info_idx], &lines,
                   &locations_buff[i]);
  }
  struct ddprof_ffi_Sample sample = {
      .locations = {.ptr = locations_buff, uw_output->nb_locs},
      .values = {.ptr = values, .len = pprof->_nb_values},
      .labels = {.ptr = NULL, 0},
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
