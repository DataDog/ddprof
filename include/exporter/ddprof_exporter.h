// Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
// This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ddprof_defs.h"
#include "ddres_def.h"
#include "exporter_input.h"
#include "perf_option.h"
#include "string_view.h"

typedef struct ddprof_ffi_ProfileExporterV3 ddprof_ffi_ProfileExporterV3;
typedef struct ddprof_ffi_Profile ddprof_ffi_Profile;
typedef struct UserTags UserTags;

typedef struct DDProfExporter {
  ExporterInput _input;
  char *_url; // url contains path and port
  bool _agent;
  bool _export;              // debug mode : should we send profiles ?
  const char *_debug_folder; // write pprofs to folder
  ddprof_ffi_ProfileExporterV3 *_exporter;
} DDProfExporter;

DDRes ddprof_exporter_init(const ExporterInput *exporter_input,
                           DDProfExporter *exporter);

DDRes ddprof_exporter_new(const UserTags *user_tags, DDProfExporter *exporter);

DDRes ddprof_exporter_export(const struct ddprof_ffi_Profile *profile,
                             DDProfExporter *exporter);

DDRes ddprof_exporter_free(DDProfExporter *exporter);

#ifdef __cplusplus
}
#endif
