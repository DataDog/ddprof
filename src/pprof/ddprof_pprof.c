#include "pprof/ddprof_pprof.h"

#include "ddres.h"

#include <ddprof/ffi.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ddprof_consts.h"

static const unsigned long s_nanos_in_one_sec = 1000000000;

// Slice helpers
static ddprof_ffi_Slice_c_char
string_view_2_slice_c_char(const string_view *slice) {
  return (struct ddprof_ffi_Slice_c_char){.ptr = slice->ptr, .len = slice->len};
}

#define SLICE_LITERAL(str)                                                     \
  (struct ddprof_ffi_Slice_c_char) { .ptr = (str), sizeof(str) - 1 }

static ddprof_ffi_Slice_c_char ffi_empty_char_slice(void) {
  return (struct ddprof_ffi_Slice_c_char){.ptr = (NULL), 0};
}

DDRes pprof_init(DDProfPProf *pprof) {
  memset(pprof, 0, sizeof(DDProfPProf));
  return ddres_init();
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
                           int nbOptions) {
  // Create one value
  struct ddprof_ffi_ValueType perf_value_type[MAX_TYPE_WATCHER + 1];
  int value_index = 0;
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

static void write_function(const FunLoc *loc, ddprof_ffi_Function *ffi_func) {
  ffi_func->name = string_view_2_slice_c_char(&loc->funname);
  // The system name is not use on backend side for now -> leave empty
  ffi_func->system_name = ffi_empty_char_slice();
  ffi_func->filename = string_view_2_slice_c_char(&loc->srcpath);
  // For now not filed
  ffi_func->start_line = 0;
}

#define UNKNOWN_BUILD_ID ffi_empty_char_slice()

static void write_mapping(const FunLoc *loc, ddprof_ffi_Mapping *ffi_mapping) {
  ffi_mapping->memory_start = loc->map_start;
  ffi_mapping->memory_limit = loc->map_end;
  ffi_mapping->file_offset = loc->map_off;
  ffi_mapping->filename = string_view_2_slice_c_char(&loc->sopath);
  ffi_mapping->build_id = UNKNOWN_BUILD_ID;
}

static void write_location(const FunLoc *loc,
                           const ddprof_ffi_Slice_line *lines,
                           ddprof_ffi_Location *ffi_location) {
  write_mapping(loc, &ffi_location->mapping);
  ffi_location->address = loc->ip;
  ffi_location->lines = *lines;
  // Folded not handled for now
  ffi_location->is_folded = false;
}

static void write_line(const FunLoc *loc, ddprof_ffi_Line *ffi_line) {
  write_function(loc, &ffi_line->function);
  ffi_line->line = loc->line;
}

// Assumption of API is that sample is valid in a single type
DDRes pprof_aggregate(const UnwindOutput *uw_output, uint64_t value,
                      int watcher_idx, DDProfPProf *pprof) {

  ddprof_ffi_Profile *profile = pprof->_profile;

  int64_t values[MAX_TYPE_WATCHER + 1] = {0};
  // Counted a single time
  values[0] = 1;
  // Add a value to the watcher we are sampling, leave others zeroed
  values[watcher_idx + 1] = value;

  ddprof_ffi_Location locations_buff[DD_MAX_STACK];
  // assumption of single line per loc for now
  ddprof_ffi_Line line_buff[DD_MAX_STACK];

  const FunLoc *locs = uw_output->locs;
  for (int i = 0; i < uw_output->idx; ++i) {
    // possibly several lines to handle inlined function (not handled for now)
    ddprof_ffi_Slice_line lines = {.ptr = &line_buff[i], .len = 1};
    write_line(&locs[i], &line_buff[i]);
    write_location(&locs[i], &lines, &locations_buff[i]);
  }

  struct ddprof_ffi_Sample sample = {
      .value = {.ptr = values, .len = pprof->_nb_values},
      .label = {.ptr = NULL, 0},
      .locations = {.ptr = locations_buff, uw_output->idx},
  };

  uint64_t id_sample = ddprof_ffi_Profile_add(profile, sample);
  if (id_sample == 0) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "Unable to add profile");
  }

  return ddres_init();
}

DDRes pprof_reset(DDProfPProf *pprof) {
  if (!ddprof_ffi_Profile_reset(pprof->_profile)) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PPROF, "Unable to add profile");
  }
  return ddres_init();
}
