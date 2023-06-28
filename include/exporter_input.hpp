// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddres.hpp"
#include "string_view.hpp"

#include <stdlib.h>

#define USERAGENT_DEFAULT "ddprof"
#define LANGUAGE_DEFAULT "native"
#define FAMILY_DEFAULT "native"

struct ExporterInput {
  std::string api_key;     // Datadog api key [hidden]
  std::string environment; // ex: staging / local / prod
  std::string service;
  std::string service_version; // appended to tags (example: 1.2.1)
  std::string host;            // agent host ex:162.184.2.1
  std::string url;             // url (can contain port and schema)
  std::string port; // port appended to the host IP (ignored in agentless)
  std::string debug_pprof_prefix; // local pprof prefix (debug)
  bool do_export{true};           // prevent exports if needed (debug flag)
  std::string_view user_agent{
      USERAGENT_DEFAULT}; // ignored for now (override in shared lib)
  std::string_view language{
      LANGUAGE_DEFAULT}; // appended to the tags (set to native)
  std::string_view family{FAMILY_DEFAULT};
  std::string_view profiler_version;
  bool agentless{false}; // Whether or not to actually use API key/intake
};
