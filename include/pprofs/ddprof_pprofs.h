#pragma once

#include "ddprof_consts.h"
#include "ddres_def.h"
#include "perf_option.h"
#include "unwind_output.h"

typedef struct ddprof_ffi_Profile ddprof_ffi_Profile;

typedef struct DDProfPProfs {
  /* single profile gathering several value types */
  ddprof_ffi_Profile *_profile;
  int _nb_values;
} DDProfPProfs;

DDRes pprofs_init(DDProfPProfs *pprofs);

DDRes pprofs_create_profile(DDProfPProfs *pprofs, const PerfOption *options,
                            int nbOptions);

/**
 * Aggregate to the existing profile the provided unwinding output.
 * @param uw_output
 * @param value matching the watcher type (ex : cpu period)
 * @param watcher_idx matches the registered order at profile creation
 * @param pprofs
 */
DDRes pprofs_aggregate(const UnwindOutput *uw_output, uint64_t value,
                       int watcher_idx, DDProfPProfs *pprofs);

DDRes ddprof_write_profile(const DDProfPProfs *pprofs, int fd);

DDRes pprofs_free_profile(DDProfPProfs *pprofs);
