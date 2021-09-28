#include "exporter/ddprof_exporter.h"

#include <assert.h>
#include <ddprof/ffi.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ddres.h"

static const int k_timeout_ms = 10000;
static const int k_size_api_key = 32;

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
  char* url = (char *)malloc(expected_size + 1);
  if (!url) // Early exit on alloc failure
    return NULL;

  snprintf(url, expected_size + 1, "%s%s:%s", protocol, host, port);
  return url;
}

static DDRes create_pprof_file(ddprof_ffi_Timespec start,
                               ddprof_ffi_Timespec end, const char *dbg_folder,
                               int *fd) {
  char time_start[128] = {0};
  struct tm *tm_start = localtime(&start.seconds);
  strftime(time_start, sizeof time_start, "%Y-%m-%dT%H:%M:%SZ", tm_start);

  char time_end[128] = {0};
  struct tm *tm_end = localtime(&end.seconds);
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
    // warning : should not contain intake.profile. (prepended in exporter)
    exporter->_url = strdup(exporter_input->host);
  }
  if (!exporter->_url) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_EXPORTER, "Failed to write url");
  }
  LG_NTC("[EXPORTER] URL %s", exporter->_url);

  // Debug process : capture pprof to a folder
  exporter->_debug_folder = getenv("DDPROF_PPROFS_FOLDER");

  return ddres_init();
}

static const int k_max_tags = 10;

static int fill_tags(const DDProfExporter *exporter, ddprof_ffi_Tag *tags_) {
  int idx_tags = 0;
  tags_[idx_tags].name = char_star_to_byteslice("language");
  tags_[idx_tags++].value = string_view_to_byteslice(exporter->_input.language);
  if (exporter->_input.environment) {
    tags_[idx_tags].name = char_star_to_byteslice("env");
    tags_[idx_tags++].value =
        char_star_to_byteslice(exporter->_input.environment);
  }
  if (exporter->_input.serviceversion) {
    tags_[idx_tags].name = char_star_to_byteslice("version");
    tags_[idx_tags++].value =
        char_star_to_byteslice(exporter->_input.serviceversion);
  }
  if (exporter->_input.service) {
    tags_[idx_tags].name = char_star_to_byteslice("service");
    tags_[idx_tags++].value = char_star_to_byteslice(exporter->_input.service);
  }

  if (exporter->_input.profiler_version.len) {
    tags_[idx_tags].name = char_star_to_byteslice("profiler-version");
    tags_[idx_tags++].value =
        string_view_to_byteslice(exporter->_input.profiler_version);
  }

  return idx_tags;
}

DDRes ddprof_exporter_new(DDProfExporter *exporter) {
  ddprof_ffi_Tag tags_[k_max_tags];
  int nb_tags = fill_tags(exporter, tags_);
  assert(nb_tags < k_max_tags);
  ddprof_ffi_Slice_tag tags = {.ptr = tags_, .len = nb_tags};

  ddprof_ffi_ByteSlice base_url = char_star_to_byteslice(exporter->_url);
  ddprof_ffi_EndpointV3 endpoint;
  if (exporter->_agent) {
    endpoint = ddprof_ffi_EndpointV3_agent(base_url);
  } else {
    ddprof_ffi_ByteSlice api_key =
        char_star_to_byteslice(exporter->_input.apikey);
    endpoint = ddprof_ffi_EndpointV3_agentless(base_url, api_key);
  }

  exporter->_exporter = ddprof_ffi_ProfileExporterV3_new(
      string_view_to_byteslice(exporter->_input.family), tags, endpoint);

  if (!exporter->_exporter) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_EXPORTER, "Failure creating exporter.");
  }
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

  LG_NTC("[EXPORTER] Export buffer of size %lu", profile_buffer.len);

  // Backend has some logic based on the following naming
  ddprof_ffi_File files_[] = {{
      .name = char_star_to_byteslice("auto.pprof"),
      .file = &profile_buffer,
  }};

  struct ddprof_ffi_Slice_file files = {.ptr = files_,
                                        .len = sizeof files_ / sizeof *files_};

  ddprof_ffi_Request *request = ddprof_ffi_ProfileExporterV3_build(
      exporter->_exporter, start, end, files, k_timeout_ms);
  if (request) {
    struct ddprof_ffi_SendResult result =
        ddprof_ffi_ProfileExporterV3_send(exporter->_exporter, request);

    if (result.tag == DDPROF_FFI_SEND_RESULT_FAILURE) {
      LG_WRN("[EXPORTER] Failure to send message (%lu-%lu)", result.failure.len,
             result.failure.capacity);
      // The following lines can print error messages. Though for now this
      // overflows so do not use.
      /*
       LG_WRN("Failure to send profiles (%*s)", (int)result.failure.len,
       error_msg);
      */
      // Free error buffer (prefer this API to the free API)
      ddprof_ffi_Buffer_reset(&result.failure);
      res = ddres_warn(DD_WHAT_EXPORTER);
    }
    /* # TODO insert retry around here ?? On what type of errors */
  } else {
    LG_ERR("[EXPORTER] Failure to build request");
    res = ddres_error(DD_WHAT_EXPORTER);
  }
  ddprof_ffi_EncodedProfile_delete(encoded_profile);
  return res;
}

DDRes ddprof_exporter_free(DDProfExporter *exporter) {
  if (exporter->_exporter)
    ddprof_ffi_ProfileExporterV3_delete(exporter->_exporter);
  exporter->_exporter = NULL;
  exporter_input_free(&exporter->_input);
  free(exporter->_url);
  exporter->_url = NULL;
  return ddres_init();
}
