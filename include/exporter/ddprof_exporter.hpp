// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"
#include "ddres_def.hpp"
#include "exporter_input.hpp"
#include "perf_watcher.hpp"
#include "tags.hpp"

#include <datadog/common.h>

struct ddog_prof_ProfileExporter;
struct ddog_prof_Profile;

namespace ddprof {

struct UserTags;

struct DDProfExporter {
  ExporterInput _input;
  std::string _url;                // url contains path and port
  std::string _debug_pprof_prefix; // write pprofs to folder
  ddog_prof_ProfileExporter _exporter{};
  bool _agent{false};
  bool _export{false}; // debug mode : should we send profiles ?
  int32_t _nb_consecutive_errors{0};
};

DDRes ddprof_exporter_init(const ExporterInput &exporter_input,
                           DDProfExporter *exporter);

DDRes ddprof_exporter_new(const UserTags *user_tags, DDProfExporter *exporter);

DDRes ddprof_exporter_export(ddog_prof_Profile *profile,
                             const Tags &additional_tags, uint32_t profile_seq,
                             DDProfExporter *exporter);

DDRes ddprof_exporter_free(DDProfExporter *exporter);

} // namespace ddprof
