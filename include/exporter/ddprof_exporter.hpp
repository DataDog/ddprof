// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <memory>

#include "ddprof_defs.hpp"
#include "ddres_def.hpp"
#include "exporter_input.hpp"
#include "perf_watcher.hpp"
#include "string_view.hpp"
#include "tags.hpp"

typedef struct ddog_ProfileExporter ddog_ProfileExporter;
typedef struct ddog_Profile ddog_Profile;
typedef struct UserTags UserTags;

#define K_NB_CONSECUTIVE_ERRORS_ALLOWED 3

typedef struct DDProfExporter {
  ExporterInput _input;
  std::string _url;                // url contains path and port
  std::string _debug_pprof_prefix; // write pprofs to folder
  ddog_ProfileExporter *_exporter;
  bool _agent;
  bool _export; // debug mode : should we send profiles ?
  int32_t _nb_consecutive_errors;
  int64_t _last_pprof_size;
} DDProfExporter;

DDRes ddprof_exporter_init(const ExporterInput *exporter_input,
                           std::shared_ptr<DDProfExporter> &exporter);

DDRes ddprof_exporter_new(const UserTags *user_tags, DDProfExporter &exporter);

DDRes ddprof_exporter_export(const struct ddog_Profile &profile,
                             const ddprof::Tags &additional_tags,
                             uint32_t profile_seq, DDProfExporter &exporter);

DDRes ddprof_exporter_free(DDProfExporter &exporter);
