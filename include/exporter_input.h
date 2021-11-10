// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "ddres.h"
#include "string_view.h"

#include <stdlib.h>

#define USERAGENT_DEFAULT "ddprof"
#define LANGUAGE_DEFAULT "native"
#define FAMILY_DEFAULT "native"

typedef struct ExporterInput {
  const char *apikey;      // Datadog api key
  const char *environment; // ex: staging / local / prod
  const char *host;        // agent host ex:162.184.2.1
  const char *site;        // not used for now
  const char *port; // port appended to the host IP (ignored in agentless)
  const char
      *service; // service to identify the profiles (ex:prof-probe-native)
  const char *serviceversion; // appended to tags (example: 1.2.1)
  const char *do_export;      // prevent exports if needed (debug flag)
  string_view user_agent;     // ignored for now (override in shared lib)
  string_view language;       // appended to the tags (set to native)
  string_view family;
  string_view profiler_version;
} ExporterInput;

// Proposals for other arguments (tags)
// - intake version : Not handled (set inside libddprof)
// - runtime
// - OS

// Possible improvement : create X table to factorize this code

#define DUP_PARAM(key_name)                                                    \
  if (src->key_name) {                                                         \
    dest->key_name = strdup(src->key_name);                                    \
    if (!dest->key_name) {                                                     \
      return ddres_error(DD_WHAT_ARGUMENT);                                    \
    }                                                                          \
  } else                                                                       \
    dest->key_name = NULL;

static inline void exporter_input_dflt(ExporterInput *exp_input) {
  exp_input->family = STRING_VIEW_LITERAL(FAMILY_DEFAULT);
  exp_input->language = STRING_VIEW_LITERAL(LANGUAGE_DEFAULT);
  exp_input->user_agent = STRING_VIEW_LITERAL(USERAGENT_DEFAULT);
  exp_input->profiler_version = str_version();
}

static inline DDRes exporter_input_copy(const ExporterInput *src,
                                        ExporterInput *dest) {
  DUP_PARAM(apikey);
  DUP_PARAM(environment);
  DUP_PARAM(host);
  DUP_PARAM(site);
  DUP_PARAM(port);
  DUP_PARAM(service);
  DUP_PARAM(serviceversion);
  DUP_PARAM(do_export);
  dest->user_agent = src->user_agent;
  dest->language = src->language;
  dest->family = src->family;
  dest->profiler_version = src->profiler_version;
  return ddres_init();
}

// free the allocated strings
static inline void exporter_input_free(ExporterInput *exporter_input) {
  free((char *)exporter_input->apikey);
  free((char *)exporter_input->environment);
  free((char *)exporter_input->host);
  free((char *)exporter_input->site);
  free((char *)exporter_input->port);
  free((char *)exporter_input->service);
  free((char *)exporter_input->serviceversion);
  free((char *)exporter_input->do_export);
}
