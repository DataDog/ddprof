// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "exporter/ddprof_exporter.hpp"

#include "ddog_profiling_utils.hpp"
#include "ddprof_cmdline.hpp"
#include "ddres.hpp"
#include "defer.hpp"
#include "tags.hpp"

#include <algorithm>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string_view>
#include <time.h>
#include <unistd.h>
#include <vector>

#include <fstream>
#include <sstream>
#include <ostream>

static const int k_timeout_ms = 10000;
static const int k_size_api_key = 32;

static char *alloc_url_agent(const char *protocol, const char *host,
                             const char *port) {
  size_t expected_size = snprintf(NULL, 0, "%s%s:%s", protocol, host, port);
  char *url = (char *)malloc(expected_size + 1);
  if (!url) // Early exit on alloc failure
    return NULL;

  snprintf(url, expected_size + 1, "%s%s:%s", protocol, host, port);
  return url;
}

static DDRes create_pprof_file(ddog_Timespec start,
                               const char *dbg_pprof_prefix, int *fd) {
  char time_start[128] = {};
  tm tm_storage;
  tm *tm_start = gmtime_r(&start.seconds, &tm_storage);
  strftime(time_start, sizeof time_start, "%Y%m%dT%H%M%SZ", tm_start);

  char filename[400];
  snprintf(filename, 400, "%s%s.pprof", dbg_pprof_prefix, time_start);
  LG_NTC("[EXPORTER] Writing pprof to file %s", filename);
  (*fd) = open(filename, O_CREAT | O_RDWR, 0600);
  DDRES_CHECK_INT((*fd), DD_WHAT_EXPORTER, "Failure to create pprof file");
  return ddres_init();
}

/// Write pprof to a valid file descriptor : allows to use pprof tools
static DDRes write_profile(const ddog_EncodedProfile *encoded_profile, int fd) {
  const ddog_Vec_u8 *buffer = &encoded_profile->buffer;
  if (write(fd, buffer->ptr, buffer->len) == 0) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_EXPORTER,
                           "Failed to write byte buffer to stdout! %s\n",
                           strerror(errno));
  }
  return ddres_init();
}

static DDRes write_pprof_file(const ddog_EncodedProfile *encoded_profile,
                              const char *dbg_pprof_prefix) {
  int fd = -1;
  DDRES_CHECK_FWD(
      create_pprof_file(encoded_profile->start, dbg_pprof_prefix, &fd));
  defer { close(fd); };
  DDRES_CHECK_FWD(write_profile(encoded_profile, fd));
  return {};
}

DDRes ddprof_exporter_init(const ExporterInput *exporter_input,
                           DDProfExporter *exporter) {
  *exporter = {};

  DDRES_CHECK_FWD(exporter_input_copy(exporter_input, &exporter->_input));
  // if we have an API key we assume we are heading for intake (slightly
  // fragile #TODO add a parameter)

  if (exporter_input->agentless && exporter_input->api_key &&
      strlen(exporter_input->api_key) >= k_size_api_key) {
    LG_NTC("[EXPORTER] Targeting intake instead of agent (API Key available)");
    exporter->_agent = false;
  } else {
    exporter->_agent = true;
    LG_NTC("[EXPORTER] Targeting agent mode (no API key)");
  }

  if (exporter->_agent) {
    exporter->_url =
        alloc_url_agent("http://", exporter_input->host, exporter_input->port);
  } else {
    // site is the usual option for intake
    if (exporter->_input.site) {
      // warning : should not contain intake.profile. (prepended in
      // libdatadog_profiling)
      exporter->_url = strdup(exporter_input->site);
    } else {
      LG_WRN(
          "[EXPORTER] Agentless - Attempting to use host (%s) instead of empty "
          "site",
          exporter_input->host);
      exporter->_url = strdup(exporter_input->host);
    }
  }
  if (!exporter->_url) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_EXPORTER, "Failed to write url");
  }
  LG_NTC("[EXPORTER] URL %s", exporter->_url);

  // Debug process : capture pprof to a folder
  exporter->_debug_pprof_prefix = exporter->_input.debug_pprof_prefix;
  exporter->_export = arg_yesno(exporter->_input.do_export, 1);

  exporter->_last_pprof_size = -1;

  return ddres_init();
}

static DDRes add_single_tag(ddog_Vec_tag &tags_exporter, std::string_view key,
                            std::string_view value) {
  ddog_PushTagResult push_tag_res =
      ddog_Vec_tag_push(&tags_exporter, to_CharSlice(key), to_CharSlice(value));
  defer { ddog_PushTagResult_drop(push_tag_res); };
  if (push_tag_res.tag == DDOG_PUSH_TAG_RESULT_ERR) {
    LG_ERR("[EXPORTER] Failure generate tag (%.*s)", (int)push_tag_res.err.len,
           push_tag_res.err.ptr);
    DDRES_RETURN_ERROR_LOG(DD_WHAT_EXPORTER, "Failed to generate tags");
  }
  return ddres_init();
}

static DDRes fill_stable_tags(const UserTags *user_tags,
                              const DDProfExporter *exporter,
                              ddog_Vec_tag &tags_exporter) {

  // language is guaranteed to be filled
  DDRES_CHECK_FWD(
      add_single_tag(tags_exporter, "language",
                     std::string_view(exporter->_input.language.ptr,
                                      exporter->_input.language.len)));

  if (exporter->_input.environment)
    DDRES_CHECK_FWD(
        add_single_tag(tags_exporter, "env", exporter->_input.environment));

  if (exporter->_input.service_version)
    DDRES_CHECK_FWD(add_single_tag(tags_exporter, "version",
                                   exporter->_input.service_version));

  if (exporter->_input.service)
    DDRES_CHECK_FWD(
        add_single_tag(tags_exporter, "service", exporter->_input.service));

  if (exporter->_input.profiler_version.len)
    DDRES_CHECK_FWD(add_single_tag(
        tags_exporter, "profiler_version",
        std::string_view(exporter->_input.profiler_version.ptr,
                         exporter->_input.profiler_version.len)));

  for (auto &el : user_tags->_tags) {
    DDRES_CHECK_FWD(add_single_tag(tags_exporter, el.first, el.second));
  }
  return ddres_init();
}

DDRes ddprof_exporter_new(const UserTags *user_tags, DDProfExporter *exporter) {
  ddog_Vec_tag tags_exporter = ddog_Vec_tag_new();
  fill_stable_tags(user_tags, exporter, tags_exporter);

  ddog_CharSlice base_url = to_CharSlice(exporter->_url);
  ddog_Endpoint endpoint;
  if (exporter->_agent) {
    endpoint = ddog_Endpoint_agent(base_url);
  } else {
    ddog_CharSlice api_key = to_CharSlice(exporter->_input.api_key);
    endpoint = ddog_Endpoint_agentless(base_url, api_key);
  }

  ddog_NewProfileExporterResult new_exporter = ddog_ProfileExporter_new(
      to_CharSlice(exporter->_input.family), &tags_exporter, endpoint);

  if (new_exporter.tag == DDOG_NEW_PROFILE_EXPORTER_RESULT_OK) {
    exporter->_exporter = new_exporter.ok;
  } else {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_EXPORTER, "Failure creating exporter - %s",
                           new_exporter.err.ptr);
    ddog_NewProfileExporterResult_drop(new_exporter);
  }
  ddog_Vec_tag_drop(tags_exporter);
  return ddres_init();
}

static DDRes check_send_response_code(uint16_t send_response_code) {
  LG_DBG("[EXPORTER] HTTP Response code: %u", send_response_code);
  if (send_response_code >= 200 && send_response_code < 300) {
    // Although we expect only 200, this range represents sucessful sends
    if (send_response_code != 200) {
      LG_NTC("[EXPORTER] HTTP Response code %u (success)", send_response_code);
    }
    return ddres_init();
  }
  if (send_response_code == 504) {
    LG_WRN("[EXPORTER] Error 504 (Timeout) - Dropping profile");
    // TODO - implement retry
    return ddres_init();
  }
  if (send_response_code == 403) {
    LG_ERR("[EXPORTER] Error 403 (Forbidden) - Check API key");
    return ddres_error(DD_WHAT_EXPORTER);
  }
  if (send_response_code == 404) {
    LG_ERR("[EXPORTER] Error 404 (Not found) - Profiles not accepted");
    return ddres_error(DD_WHAT_EXPORTER);
  }
  LG_WRN("[EXPORTER] Error sending data - HTTP code %u (continue profiling)",
         send_response_code);
  return ddres_init();
}

static DDRes fill_cycle_tags(const ddprof::Tags &additional_tags,
                             uint32_t profile_seq,
                             ddog_Vec_tag &ffi_additional_tags) {

  DDRES_CHECK_FWD(add_single_tag(ffi_additional_tags, "profile_seq",
                                 std::to_string(profile_seq)));

  for (const auto &el : additional_tags) {
    DDRES_CHECK_FWD(add_single_tag(ffi_additional_tags, el.first, el.second));
  }
  return ddres_init();
}

DDRes ddprof_exporter_export(const ddog_Profile *profile,
                             const ddprof::Tags &additional_tags,
                             uint32_t profile_seq, DDProfExporter *exporter) {
  DDRes res = ddres_init();
  ddog_SerializeResult serialized_result =
      ddog_Profile_serialize(profile, nullptr, nullptr);
  defer { ddog_SerializeResult_drop(serialized_result); };
  if (serialized_result.tag != DDOG_SERIALIZE_RESULT_OK) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_EXPORTER, "Failed to serialize");
  }

  ddog_EncodedProfile *encoded_profile = &serialized_result.ok;

  if (exporter->_debug_pprof_prefix) {
    write_pprof_file(encoded_profile, exporter->_debug_pprof_prefix);
  }

  ddog_Timespec start = encoded_profile->start;
  ddog_Timespec end = encoded_profile->end;

  ddog_ByteSlice profile_data = {
      .ptr = encoded_profile->buffer.ptr,
      .len = encoded_profile->buffer.len,
  };

  exporter->_last_pprof_size = profile_data.len;

  if (exporter->_export) {
    ddog_Vec_tag ffi_additional_tags = ddog_Vec_tag_new();
    defer { ddog_Vec_tag_drop(ffi_additional_tags); };
    DDRES_CHECK_FWD(
        fill_cycle_tags(additional_tags, profile_seq, ffi_additional_tags););

    LG_NTC("[EXPORTER] Export buffer of size %lu", profile_data.len);

    uint64_t json_start = start.seconds*1e9 + start.nanoseconds;
    uint64_t json_end = end.seconds*1e9 + end.nanoseconds;
    nlohmann::json ret = exporter->noisy_neighbors->finalize(json_end);
    ret["timeRange"]["startNs"] = json_start;
    ret["timeRange"]["endNs"] = json_end;

    // Also add a version tag
    if (exporter->_input.service_version)
      ret["version"] = exporter->_input.service_version;

    std::ofstream jsoncpy("/tmp/noisy.timeline.json");
    std::stringstream timeline_json = {};
    timeline_json << ret;
    std::string json_str = timeline_json.str();
    jsoncpy << json_str;

    ddog_ByteSlice timeline_data {
      .ptr = (const uint8_t*)json_str.c_str(),
      .len = json_str.size(),
    };

    // Now reset the noisy neighbor state
    exporter->noisy_neighbors->clear();

    // Backend has some logic based on the following naming
    ddog_File files_[] = {{
        .name = to_CharSlice("auto.pprof"),
        .file = profile_data,
    }, {
        .name = to_CharSlice("timeline.json"),
        .file = timeline_data,
    }};
    ddog_Slice_file files = {.ptr = files_, .len = std::size(files_)};

    ddog_Request *request =
        ddog_ProfileExporter_build(exporter->_exporter, start, end, files,
                                   &ffi_additional_tags, k_timeout_ms);
    if (request) {
      ddog_SendResult result =
          ddog_ProfileExporter_send(exporter->_exporter, request, nullptr);
      defer { ddog_SendResult_drop(result); };

      if (result.tag == DDOG_SEND_RESULT_ERR) {
        LG_WRN("Failure to establish connection, check url %s", exporter->_url);
        LG_WRN("Failure to send profiles (%.*s)", (int)result.err.len,
               result.err.ptr);
        // Free error buffer (prefer this API to the free API)
        if (exporter->_nb_consecutive_errors++ >=
            K_NB_CONSECUTIVE_ERRORS_ALLOWED) {
          // this will shut down profiler
          res = ddres_error(DD_WHAT_EXPORTER);
        } else {
          res = ddres_warn(DD_WHAT_EXPORTER);
        }
      } else {
        // success establishing connection
        exporter->_nb_consecutive_errors = 0;
        res = check_send_response_code(result.http_response.code);
      }
    } else {
      LG_ERR("[EXPORTER] Failure to build request");
      res = ddres_error(DD_WHAT_EXPORTER);
    }
  }
  return res;
}

DDRes ddprof_exporter_free(DDProfExporter *exporter) {
  if (exporter->_exporter)
    ddog_ProfileExporter_delete(exporter->_exporter);
  exporter->_exporter = nullptr;
  exporter_input_free(&exporter->_input);
  free(exporter->_url);
  exporter->_url = nullptr;
  return ddres_init();
}
