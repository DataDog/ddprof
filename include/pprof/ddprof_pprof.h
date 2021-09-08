#pragma once

#include "ddprof_consts.h"
#include "ddres_def.h"
#include "perf_option.h"
#include "unwind_output.h"

typedef struct ddprof_ffi_Profile ddprof_ffi_Profile;

typedef struct DDProfPProf {
  /* single profile gathering several value types */
  ddprof_ffi_Profile *_profile;
  int _nb_values;
} DDProfPProf;

DDRes pprof_create_profile(DDProfPProf *pprof, const PerfOption *options,
                           int nbOptions);

/**
 * Aggregate to the existing profile the provided unwinding output.
 * @param uw_output
 * @param value matching the watcher type (ex : cpu period)
 * @param watcher_idx matches the registered order at profile creation
 * @param pprof
 */
DDRes pprof_aggregate(const UnwindOutput *uw_output, uint64_t value,
                      int watcher_idx, DDProfPProf *pprof);

DDRes pprof_reset(DDProfPProf *pprof);

DDRes pprof_write_profile(const DDProfPProf *pprof, int fd);

DDRes pprof_free_profile(DDProfPProf *pprof);
