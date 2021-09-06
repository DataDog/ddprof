#pragma once

#include "ddres.h"
#include "string_view.h"

#include <stdlib.h>

/*
Extract of things to support :
#define DDR_PARAMS(X) \
  \
  X(PROFILERVERSION, profiler_version, 3, "profiler-version", 0, NULL) \
*/

typedef struct ExporterInput {
  const char *apikey;      // Datadog api key
  const char *environment; // ex: staging / local / prod
  const char *host;        // agent host ex:162.184.2.1
  const char *site;        // not used for now
  const char *port; // port appended to the host IP (ignored in agentless)
  const char
      *service; // service to identify the profiles (ex:prof-probe-native)
  const char *serviceversion; // appended to tags (example: 1.2.1)
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

static inline DDRes exporter_input_copy(const ExporterInput *src,
                                        ExporterInput *dest) {
  DUP_PARAM(apikey);
  DUP_PARAM(environment);
  DUP_PARAM(host);
  DUP_PARAM(site);
  DUP_PARAM(port);
  DUP_PARAM(service);
  DUP_PARAM(serviceversion);
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
}
