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

#include <absl/strings/str_cat.h>
#include <absl/strings/substitute.h>
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace ddprof {

namespace {
constexpr int k_timeout_ms{10000};
constexpr int k_max_nb_consecutive_errors_allowed{3};

std::string alloc_url_agent(std::string_view protocol, std::string_view host,
                            std::string_view port) {
  if (!port.empty()) {
    return absl::Substitute("$0$1:$2", protocol, host, port);
  }
  return absl::StrCat(protocol, host);
}

DDRes create_pprof_file(ddog_Timespec start, const char *dbg_pprof_prefix,
                        int *fd) {
  constexpr size_t k_max_time_length = 128;
  char time_start[k_max_time_length] = {};
  tm tm_storage;
  tm *tm_start = gmtime_r(&start.seconds, &tm_storage);
  strftime(time_start, std::size(time_start), "%Y%m%dT%H%M%SZ", tm_start);

  char filename[PATH_MAX];
  snprintf(filename, std::size(filename), "%s%s.pprof.lz4", dbg_pprof_prefix,
           time_start);
  LG_NTC("[EXPORTER] Writing pprof to file %s", filename);
  constexpr int read_write_user_only = 0600;
  (*fd) = open(filename, O_CREAT | O_RDWR, read_write_user_only);
  DDRES_CHECK_INT((*fd), DD_WHAT_EXPORTER, "Failure to create pprof file");
  return {};
}

/// Write pprof to a valid file descriptor : allows to use pprof tools
DDRes write_profile(const ddog_prof_EncodedProfile *encoded_profile, int fd) {
  const ddog_Vec_U8 *buffer = &encoded_profile->buffer;
  if (write(fd, buffer->ptr, buffer->len) == 0) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_EXPORTER,
                           "Failed to write byte buffer to stdout! %s\n",
                           strerror(errno));
  }
  return {};
}

DDRes write_pprof_file(const ddog_prof_EncodedProfile *encoded_profile,
                       const char *dbg_pprof_prefix) {
  int fd = -1;
  DDRES_CHECK_FWD(
      create_pprof_file(encoded_profile->start, dbg_pprof_prefix, &fd));
  defer { close(fd); };
  DDRES_CHECK_FWD(write_profile(encoded_profile, fd));
  return {};
}

bool contains_port(std::string_view url) {
  if (url.empty()) {
    return false;
  }
  auto pos = url.rfind(':'); // Find the last occurrence of ':'
  if (pos == std::string_view::npos) {
    return false; // No ':' found
  }
  auto port_sv = url.substr(pos + 1);
  return std::all_of(port_sv.begin(), port_sv.end(), ::isdigit);
}

DDRes add_single_tag(ddog_Vec_Tag &tags_exporter, std::string_view key,
                     std::string_view value) {
  ddog_Vec_Tag_PushResult push_tag_res =
      ddog_Vec_Tag_push(&tags_exporter, to_CharSlice(key), to_CharSlice(value));
  if (push_tag_res.tag == DDOG_VEC_TAG_PUSH_RESULT_ERR) {
    defer { ddog_Error_drop(&push_tag_res.err); };

    LG_ERR("[EXPORTER] Failure generate tag (%.*s)",
           (int)push_tag_res.err.message.len, push_tag_res.err.message.ptr);
    DDRES_RETURN_ERROR_LOG(DD_WHAT_EXPORTER, "Failed to generate tags");
  }
  return {};
}

DDRes fill_stable_tags(const UserTags *user_tags,
                       const DDProfExporter *exporter,
                       ddog_Vec_Tag &tags_exporter) {

  // language is guaranteed to be filled
  DDRES_CHECK_FWD(
      add_single_tag(tags_exporter, "language", exporter->_input.language));

  if (!exporter->_input.environment.empty()) {
    DDRES_CHECK_FWD(
        add_single_tag(tags_exporter, "env", exporter->_input.environment));
  }

  if (!exporter->_input.service_version.empty()) {
    DDRES_CHECK_FWD(add_single_tag(tags_exporter, "version",
                                   exporter->_input.service_version));
  }

  if (!exporter->_input.service.empty()) {
    DDRES_CHECK_FWD(
        add_single_tag(tags_exporter, "service", exporter->_input.service));
  }

  if (!exporter->_input.profiler_version.empty()) {
    DDRES_CHECK_FWD(add_single_tag(tags_exporter, "profiler_version",
                                   exporter->_input.profiler_version));
  }

  DDRES_CHECK_FWD(
      add_single_tag(tags_exporter, "runtime-id", exporter->_input.runtime_id));

  for (const auto &el : user_tags->_tags) {
    DDRES_CHECK_FWD(add_single_tag(tags_exporter, el.first, el.second));
  }
  return {};
}

DDRes check_send_response_code(uint16_t send_response_code) {
  constexpr int k_http_gateway_timeout = 504;
  constexpr int k_http_forbidden = 403;
  constexpr int k_http_not_found = 404;
  constexpr int k_http_ok = 200;
  constexpr int k_http_multiple_choices = 300;

  LG_DBG("[EXPORTER] HTTP Response code: %u", send_response_code);
  if (send_response_code >= k_http_ok &&
      send_response_code < k_http_multiple_choices) {
    // Although we expect only 200, this range represents sucessful sends
    if (send_response_code != k_http_ok) {
      LG_NTC("[EXPORTER] HTTP Response code %u (success)", send_response_code);
    }
    return {};
  }
  if (send_response_code == k_http_gateway_timeout) {
    LG_WRN("[EXPORTER] Error 504 (Timeout) - Dropping profile");
    // TODO - implement retry
    return {};
  }
  if (send_response_code == k_http_forbidden) {
    LG_ERR("[EXPORTER] Error 403 (Forbidden) - Check API key");
    return ddres_error(DD_WHAT_EXPORTER);
  }
  if (send_response_code == k_http_not_found) {
    LG_ERR("[EXPORTER] Error 404 (Not found) - Profiles not accepted");
    return ddres_error(DD_WHAT_EXPORTER);
  }
  LG_WRN("[EXPORTER] Error sending data - HTTP code %u (continue profiling)",
         send_response_code);
  return {};
}

DDRes fill_cycle_tags(const Tags &additional_tags, uint32_t profile_seq,
                      ddog_Vec_Tag &ffi_additional_tags) {

  DDRES_CHECK_FWD(add_single_tag(ffi_additional_tags, "profile_seq",
                                 std::to_string(profile_seq)));

  for (const auto &el : additional_tags) {
    DDRES_CHECK_FWD(add_single_tag(ffi_additional_tags, el.first, el.second));
  }
  return {};
}

} // namespace

DDRes ddprof_exporter_init(const ExporterInput &exporter_input,
                           DDProfExporter *exporter) {
  exporter->_input = exporter_input;
  // if we have an API key we assume we are heading for intake (slightly
  // fragile #TODO add a parameter)

  if (exporter_input.agentless && !exporter_input.api_key.empty() &&
      exporter_input.api_key.size() >= k_size_api_key) {
    LG_NTC("[EXPORTER] Targeting intake instead of agent (API Key available)");
    exporter->_agent = false;
  } else {
    exporter->_agent = true;
    LG_NTC("[EXPORTER] Targeting agent mode (no API key)");
  }

  if (exporter->_agent) {
    std::string port_str = exporter_input.port;

    if (!exporter_input.url.empty()) {
      // uds or already port -> no port
      if (!strncasecmp(exporter_input.url.c_str(), "unix", 4) ||
          contains_port(exporter_input.url)) {
        port_str = {};
      }
      // check if schema is already available
      if (strstr(exporter_input.url.c_str(), "://") != nullptr) {
        exporter->_url = alloc_url_agent("", exporter_input.url, port_str);
      } else if (exporter_input.url[0] == '/') {
        // Starts with a '/', assume unix domain socket
        exporter->_url = alloc_url_agent("unix://", exporter_input.url, {});
      } else {
        // not available, assume http
        exporter->_url =
            alloc_url_agent("http://", exporter_input.url, port_str);
      }
    } else {
      // no url, use default host and port settings
      exporter->_url =
          alloc_url_agent("http://", exporter_input.host, exporter_input.port);
    }
  } else {
    // agentless mode
    if (!exporter->_input.url.empty()) {
      // warning : should not contain intake.profile. (prepended in
      // libdatadog_profiling)
      exporter->_url = exporter->_input.url;
    } else {
      LG_WRN(
          "[EXPORTER] Agentless - Attempting to use host (%s) instead of empty "
          "url",
          exporter_input.host.c_str());
      exporter->_url =
          alloc_url_agent("http://", exporter_input.host, exporter_input.port);
    }
  }
  if (exporter->_url.empty()) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_EXPORTER, "Failed to write url");
  }
  LG_NTC("[EXPORTER] URL %s", exporter->_url.c_str());

  // Debug process : capture pprof to a folder
  exporter->_debug_pprof_prefix = exporter->_input.debug_pprof_prefix;
  exporter->_export = exporter->_input.do_export;
  return {};
}

DDRes ddprof_exporter_new(const UserTags *user_tags, DDProfExporter *exporter) {
  ddog_Vec_Tag tags_exporter = ddog_Vec_Tag_new();
  defer { ddog_Vec_Tag_drop(tags_exporter); };

  fill_stable_tags(user_tags, exporter, tags_exporter);

  ddog_CharSlice const base_url = to_CharSlice(exporter->_url);
  ddog_prof_Endpoint endpoint;
  if (exporter->_agent) {
    endpoint = ddog_prof_Endpoint_agent(base_url);
  } else {
    ddog_CharSlice const api_key = to_CharSlice(exporter->_input.api_key);
    endpoint = ddog_prof_Endpoint_agentless(base_url, api_key);
  }

  ddog_prof_Exporter_NewResult res_exporter = ddog_prof_Exporter_new(
      to_CharSlice(exporter->_input.user_agent),
      to_CharSlice(exporter->_input.profiler_version),
      to_CharSlice(exporter->_input.family), &tags_exporter, endpoint);

  if (res_exporter.tag == DDOG_PROF_EXPORTER_NEW_RESULT_OK) {
    exporter->_exporter = res_exporter.ok;
  } else {
    defer { ddog_Error_drop(&res_exporter.err); };
    DDRES_RETURN_ERROR_LOG(DD_WHAT_EXPORTER, "Failure creating exporter - %.*s",
                           static_cast<int>(res_exporter.err.message.len),
                           res_exporter.err.message.ptr);
  }
  ddog_prof_MaybeError res = ddog_prof_Exporter_set_timeout(exporter->_exporter,
                                                            k_timeout_ms);
  if (res.tag == DDOG_PROF_OPTION_ERROR_SOME_ERROR) {
    defer { ddog_MaybeError_drop(res); };
    DDRES_RETURN_ERROR_LOG(DD_WHAT_EXPORTER, "Failure setting timeout - %.*s",
                           static_cast<int>(res.some.message.len),
                           res.some.message.ptr);
  }
  return {};
}

DDRes ddprof_exporter_export(ddog_prof_Profile *profile,
                             const Tags &additional_tags, uint32_t profile_seq,
                             DDProfExporter *exporter) {
  DDRes res = ddres_init();
  ddog_prof_Profile_SerializeResult serialized_result =
      ddog_prof_Profile_serialize(profile, nullptr, nullptr, nullptr);
  if (serialized_result.tag != DDOG_PROF_PROFILE_SERIALIZE_RESULT_OK) {
    defer { ddog_Error_drop(&serialized_result.err); };
    DDRES_RETURN_ERROR_LOG(DD_WHAT_EXPORTER, "Failed to serialize: %s",
                           serialized_result.err.message.ptr);
  }

  ddog_prof_EncodedProfile *encoded_profile = &serialized_result.ok;
  defer { ddog_prof_EncodedProfile_drop(encoded_profile); };

  if (!exporter->_debug_pprof_prefix.empty()) {
    write_pprof_file(encoded_profile, exporter->_debug_pprof_prefix.c_str());
  }

  ddog_Timespec const start = encoded_profile->start;
  ddog_Timespec const end = encoded_profile->end;

  if (exporter->_export) {
    ddog_Vec_Tag ffi_additional_tags = ddog_Vec_Tag_new();
    defer { ddog_Vec_Tag_drop(ffi_additional_tags); };
    DDRES_CHECK_FWD(
        fill_cycle_tags(additional_tags, profile_seq, ffi_additional_tags););

    LG_NTC("[EXPORTER] Export buffer of size %lu", encoded_profile->buffer.len);

    // Backend has some logic based on the following naming
    ddog_prof_Exporter_File files_[] = {{
        .name = to_CharSlice("auto.pprof"),
        .file = ddog_Vec_U8_as_slice(&encoded_profile->buffer),
    }};
    ddog_prof_Exporter_Slice_File const files = {.ptr = files_,
                                                 .len = std::size(files_)};

    ddog_prof_Exporter_Request_BuildResult res_request =
        ddog_prof_Exporter_Request_build(exporter->_exporter, start, end,
                                         ddog_prof_Exporter_Slice_File_empty(),
                                         files, &ffi_additional_tags,
                                         nullptr, // optional_endpoints_stats
                                         nullptr, // internal_metadata_json
                                         nullptr, // optional_info_json
                                         );

    if (res_request.tag == DDOG_PROF_EXPORTER_REQUEST_BUILD_RESULT_OK) {
      ddog_prof_Exporter_Request *request = res_request.ok;

      // dropping the request is not useful if we have a send
      // however the send will replace the request by null when it takes
      // ownership
      defer { ddog_prof_Exporter_Request_drop(&request); };

      ddog_prof_Exporter_SendResult result =
          ddog_prof_Exporter_send(exporter->_exporter, &request, nullptr);

      if (result.tag == DDOG_PROF_EXPORTER_SEND_RESULT_ERR) {
        defer { ddog_Error_drop(&result.err); };
        LG_WRN("Failure to establish connection, check url %s",
               exporter->_url.c_str());
        LG_WRN("Failure to send profiles (%.*s)", (int)result.err.message.len,
               result.err.message.ptr);
        // Free error buffer (prefer this API to the free API)
        if (exporter->_nb_consecutive_errors++ >=
            k_max_nb_consecutive_errors_allowed) {
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
      defer { ddog_Error_drop(&res_request.err); };
      LG_ERR("[EXPORTER] Failure to build request: %s",
             res_request.err.message.ptr);
      res = ddres_error(DD_WHAT_EXPORTER);
    }
  }
  return res;
}

DDRes ddprof_exporter_free(DDProfExporter *exporter) {
  if (exporter->_exporter) {
    ddog_prof_Exporter_drop(exporter->_exporter);
  }
  exporter->_exporter = nullptr;
  return {};
}

} // namespace ddprof
