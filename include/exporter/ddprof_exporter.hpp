// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddprof_defs.hpp"
#include "ddres_def.hpp"
#include "exporter_input.hpp"
#include "perf_watcher.hpp"
#include "timeline/noisy_neighbors.hpp"
#include "string_view.hpp"
#include "tags.hpp"

#include <nlohmann/json.hpp>

typedef struct ddog_ProfileExporter ddog_ProfileExporter;
typedef struct ddog_Profile ddog_Profile;
typedef struct UserTags UserTags;

#define K_NB_CONSECUTIVE_ERRORS_ALLOWED 3

typedef struct DDProfExporter {
  ExporterInput _input;
  char *_url;                      // url contains path and port
  const char *_debug_pprof_prefix; // write pprofs to folder
  ddog_ProfileExporter *_exporter;
  bool _agent;
  bool _export; // debug mode : should we send profiles ?
  int32_t _nb_consecutive_errors;
  int64_t _last_pprof_size;
  nlohmann::json timeline_data;
  std::unique_ptr<NoisyNeighbors> noisy_neighbors;
} DDProfExporter;

DDRes ddprof_exporter_init(const ExporterInput *exporter_input,
                           DDProfExporter *exporter);

DDRes ddprof_exporter_new(const UserTags *user_tags, DDProfExporter *exporter);

DDRes ddprof_exporter_export(const struct ddog_Profile *profile,
                             const ddprof::Tags &additional_tags,
                             uint32_t profile_seq, DDProfExporter *exporter);

DDRes ddprof_exporter_free(DDProfExporter *exporter);
