#pragma once

#include "ddprof_defs.h"
#include "ddres_def.h"
#include "exporter_input.h"
#include "perf_option.h"
#include "string_view.h"

typedef struct ddprof_ffi_ProfileExporterV3 ddprof_ffi_ProfileExporterV3;
typedef struct ddprof_ffi_Profile ddprof_ffi_Profile;

typedef struct DDProfExporter {
  ExporterInput _input;
  char *_url; // url contains path and port
  bool _agent;
  const char *_debug_folder; // write pprofs to folder
  ddprof_ffi_ProfileExporterV3 *_exporter;
} DDProfExporter;

DDRes ddprof_exporter_init(const ExporterInput *exporter_input,
                           DDProfExporter *exporter);

DDRes ddprof_exporter_new(DDProfExporter *exporter);

DDRes ddprof_exporter_export(const struct ddprof_ffi_Profile *profile,
                             DDProfExporter *exporter);

DDRes ddprof_exporter_free(DDProfExporter *exporter);
