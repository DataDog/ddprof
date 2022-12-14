// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddres.hpp"

#include <stdlib.h>
#include <string>
#include <string_view>

typedef struct ExporterInput {
  std::string api_key;          // Datadog api key
  std::string environment;      // ex: staging / local / prod
  std::string host;             // agent host ex:162.184.2.1
  std::string site;             // not used for now
  std::string port;             // port appended to the host IP (ignored in agentless)
  std::string service;          // service to identify the profiles (ex:prof-probe-native)
  std::string service_version;    // appended to tags (example: 1.2.1)
  std::string do_export;          // prevent exports if needed (debug flag)
  std::string debug_pprof_prefix; // local pprof prefix (debug)
  std::string_view user_agent = "ddprof";
  std::string_view language = "native";
  std::string_view family = "native";
  std::string_view profiler_version;
  bool agentless; // Whether or not to actually use API key/intake
} ExporterInput;

// Proposals for other arguments (tags)
// - intake version : Not handled (set inside libddprof)
// - runtime
// - OS

// Possible improvement : create X table to factorize this code

static inline void exporter_input_dflt(ExporterInput *exp_input) {
  exp_input->profiler_version = str_version();
}

static inline DDRes exporter_input_copy(const ExporterInput *src,
                                        ExporterInput *dest) {
  dest->user_agent = src->user_agent;
  dest->language = src->language;
  dest->family = src->family;
  dest->profiler_version = src->profiler_version;
  return ddres_init();
}

// free the allocated strings
static inline void exporter_input_free(ExporterInput *exporter_input) {
}
