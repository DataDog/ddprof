// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "exporter/ddprof_exporter.h"

extern "C" {
#include "ddprof_cmdline.h"

#include <assert.h>
#include <ddprof/ffi.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
}

#include "ddres.h"
#include "tags.hpp"
#include <amc/vector.hpp>
#include <string>

static const int k_timeout_ms = 10000;
static const int k_size_api_key = 32;

static ddprof_ffi_ByteSlice cpp_string_to_byteslice(const std::string &str) {
  return (ddprof_ffi_ByteSlice){.ptr = (uint8_t *)str.c_str(),
                                .len = str.size()};
}

static ddprof_ffi_ByteSlice string_view_to_byteslice(string_view slice) {
  return (ddprof_ffi_ByteSlice){.ptr = (uint8_t *)slice.ptr, .len = slice.len};
}

static ddprof_ffi_ByteSlice char_star_to_byteslice(const char *string) {
  return (ddprof_ffi_ByteSlice){.ptr = (uint8_t *)string,
                                .len = strlen(string)};
}

static char *alloc_url_agent(const char *protocol, const char *host,
                             const char *port) {
  size_t expected_size = snprintf(NULL, 0, "%s%s:%s", protocol, host, port);
  char *url = (char *)malloc(expected_size + 1);
  if (!url) // Early exit on alloc failure
    return NULL;

  snprintf(url, expected_size + 1, "%s%s:%s", protocol, host, port);
  return url;
}

static DDRes create_pprof_file(ddprof_ffi_Timespec start,
                               ddprof_ffi_Timespec end, const char *dbg_folder,
                               int *fd) {
  char time_start[128] = {0};
  // struct tm *localtime_r(const time_t *timep, struct tm *result);
  struct tm tm_storage;
  struct tm *tm_start = localtime_r(&start.seconds, &tm_storage);
  strftime(time_start, sizeof time_start, "%Y-%m-%dT%H:%M:%SZ", tm_start);

  char time_end[128] = {0};
  struct tm *tm_end = localtime_r(&end.seconds, &tm_storage);
  strftime(time_end, sizeof time_end, "%Y-%m-%dT%H:%M:%SZ", tm_end);

  char filename[400];
  snprintf(filename, 400, "%s/ddprof_%s_%s.pprof", dbg_folder, time_start,
           time_end);
  LG_NTC("[EXPORTER] Writing pprof to file %s", filename);
  (*fd) = open(filename, O_CREAT | O_RDWR, 0600);
  DDRES_CHECK_INT((*fd), DD_WHAT_EXPORTER, "Failure to create pprof file");
  return ddres_init();
}

/// Write pprof to a valid file descriptor : allows to use pprof tools
static DDRes write_profile(const ddprof_ffi_EncodedProfile *encoded_profile,
                           int fd) {
  const ddprof_ffi_Buffer *buffer = &encoded_profile->buffer;
  if (write(fd, buffer->ptr, buffer->len) == 0) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_EXPORTER,
                           "Failed to write byte buffer to stdout! %s\n",
                           strerror(errno));
  }
  return ddres_init();
}

static DDRes write_pprof_file(const ddprof_ffi_EncodedProfile *encoded_profile,
                              const char *dbg_folder) {
  int fd = -1;
  create_pprof_file(encoded_profile->start, encoded_profile->end, dbg_folder,
                    &fd);
  write_profile(encoded_profile, fd);
  close(fd);
  return ddres_init();
}

extern "C" {

DDRes ddprof_exporter_init(const ExporterInput *exporter_input,
                           DDProfExporter *exporter) {
  memset(exporter, 0, sizeof(DDProfExporter));

  DDRES_CHECK_FWD(exporter_input_copy(exporter_input, &exporter->_input));
  // if we have an API key we assume we are heading for intake (slightly
  // fragile #TODO add a parameter)

  if (exporter_input->apikey &&
      strlen(exporter_input->apikey) >= k_size_api_key) {
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
      // warning : should not contain intake.profile. (prepended in libddprof)
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
  exporter->_debug_folder = getenv("DDPROF_PPROFS_FOLDER");
  exporter->_export = arg_yesno(exporter->_input.do_export, 1);
  return ddres_init();
}

static const int k_max_tags = 10;

static void
fill_tags(const UserTags *user_tags, const DDProfExporter *exporter,
          amc::SmallVector<ddprof_ffi_Tag, k_max_tags> &tags_exporter) {
  tags_exporter.push_back(ddprof_ffi_Tag{
      .name = char_star_to_byteslice("language"),
      .value = string_view_to_byteslice(exporter->_input.language)});

  if (exporter->_input.environment) {
    tags_exporter.push_back(ddprof_ffi_Tag{
        .name = char_star_to_byteslice("env"),
        .value = char_star_to_byteslice(exporter->_input.environment)});
  }

  if (exporter->_input.serviceversion) {
    tags_exporter.push_back(ddprof_ffi_Tag{
        .name = char_star_to_byteslice("version"),
        .value = char_star_to_byteslice(exporter->_input.serviceversion)});
  }

  if (exporter->_input.service) {
    tags_exporter.push_back(ddprof_ffi_Tag{
        .name = char_star_to_byteslice("service"),
        .value = char_star_to_byteslice(exporter->_input.service)});
  }

  if (exporter->_input.profiler_version.len) {
    tags_exporter.push_back(ddprof_ffi_Tag{
        .name = char_star_to_byteslice("profiler_version"),
        .value = string_view_to_byteslice(exporter->_input.profiler_version)});
  }

  std::for_each(user_tags->_tags.begin(), user_tags->_tags.end(),
                [&](ddprof::Tag const &el) {
                  tags_exporter.push_back(ddprof_ffi_Tag{
                      .name = cpp_string_to_byteslice(el.first),
                      .value = cpp_string_to_byteslice(el.second)});
                });
}

DDRes ddprof_exporter_new(const UserTags *user_tags, DDProfExporter *exporter) {
  amc::SmallVector<ddprof_ffi_Tag, k_max_tags> tags_exporter;
  fill_tags(user_tags, exporter, tags_exporter);
  ddprof_ffi_Slice_tag tags = {.ptr = &tags_exporter[0],
                               .len = tags_exporter.size()};

  ddprof_ffi_ByteSlice base_url = char_star_to_byteslice(exporter->_url);
  ddprof_ffi_EndpointV3 endpoint;
  if (exporter->_agent) {
    endpoint = ddprof_ffi_EndpointV3_agent(base_url);
  } else {
    ddprof_ffi_ByteSlice api_key =
        char_star_to_byteslice(exporter->_input.apikey);
    endpoint = ddprof_ffi_EndpointV3_agentless(base_url, api_key);
  }

  ddprof_ffi_NewProfileExporterV3Result new_exporterv3 =
      ddprof_ffi_ProfileExporterV3_new(
          string_view_to_byteslice(exporter->_input.family), tags, endpoint);

  if (new_exporterv3.tag == DDPROF_FFI_NEW_PROFILE_EXPORTER_V3_RESULT_OK) {
    exporter->_exporter = new_exporterv3.ok;
  } else {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_EXPORTER, "Failure creating exporter - %s",
                           new_exporterv3.err.ptr);
    ddprof_ffi_Buffer_reset(&new_exporterv3.err);
  }
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

DDRes ddprof_exporter_export(const struct ddprof_ffi_Profile *profile,
                             DDProfExporter *exporter) {
  DDRes res = ddres_init();
  struct ddprof_ffi_EncodedProfile *encoded_profile =
      ddprof_ffi_Profile_serialize(profile);
  if (!encoded_profile) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_EXPORTER, "Failed to serialize");
  }
  if (exporter->_debug_folder) {
    write_pprof_file(encoded_profile, exporter->_debug_folder);
  }

  ddprof_ffi_Timespec start = encoded_profile->start;
  ddprof_ffi_Timespec end = encoded_profile->end;

  ddprof_ffi_Buffer profile_buffer = {
      .ptr = encoded_profile->buffer.ptr,
      .len = encoded_profile->buffer.len,
      .capacity = encoded_profile->buffer.capacity,
  };

  if (exporter->_export) {
    LG_NTC("[EXPORTER] Export buffer of size %lu", profile_buffer.len);

    // Backend has some logic based on the following naming
    ddprof_ffi_File files_[] = {{
        .name = char_star_to_byteslice("auto.pprof"),
        .file = &profile_buffer,
    }};
    struct ddprof_ffi_Slice_file files = {
        .ptr = files_, .len = sizeof files_ / sizeof *files_};

    ddprof_ffi_Request *request = ddprof_ffi_ProfileExporterV3_build(
        exporter->_exporter, start, end, files, k_timeout_ms);
    if (request) {
      struct ddprof_ffi_SendResult result =
          ddprof_ffi_ProfileExporterV3_send(exporter->_exporter, request);

      LG_NFO("[EXPORTER] Request tag value: %u", result.tag);
      if (result.tag == DDPROF_FFI_SEND_RESULT_FAILURE) {
        LG_WRN("Failure to establish connection - check url %s",
               exporter->_url);
        // There is an overflow issue when using the error buffer from rust
        // LG_WRN("Failure to send profiles (%*s)", (int)result.failure.len,
        //        result.failure.ptr);
        // Free error buffer (prefer this API to the free API)
        ddprof_ffi_Buffer_reset(&result.failure);
        res = ddres_error(DD_WHAT_EXPORTER);
      } else {
        res = check_send_response_code(result.http_response.code);
      }
    } else {
      LG_ERR("[EXPORTER] Failure to build request");
      res = ddres_error(DD_WHAT_EXPORTER);
    }
  }
  ddprof_ffi_EncodedProfile_delete(encoded_profile);
  return res;
}

DDRes ddprof_exporter_free(DDProfExporter *exporter) {
  if (exporter->_exporter)
    ddprof_ffi_ProfileExporterV3_delete(exporter->_exporter);
  exporter->_exporter = nullptr;
  exporter_input_free(&exporter->_input);
  free(exporter->_url);
  exporter->_url = nullptr;
  return ddres_init();
}

} // extern C
