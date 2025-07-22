// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "exporter/ddprof_exporter.hpp"

#include "loghandle.hpp"
#include "pevent_lib_mocks.hpp"
#include "pprof/ddprof_pprof.hpp"
#include "tags.hpp"
#include "unwind_output_mock.hpp"

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <utility>

#include "symbol_hdr.hpp"

namespace ddprof {
// Mock
int get_nb_hw_thread() { return 2; }

// clang-format off
// How to test the exporter with a receptor
// Boot a receptor (example mockserver): docker run --name http_receptor --rm -p 1080:1080 mockserver/mockserver
// get the url : docker inspect --format '{{ .NetworkSettings.IPAddress }}' http_receptor
// Once the url is set within the docker, you can test the messages being sent
// (example : export HTTP_RECEPTOR_URL=http://172.17.0.5:1080)
// clang-format on
#define K_RECEPTOR_ENV_ADDR "HTTP_RECEPTOR_URL"

const char *get_url_from_env(const char *env_var) {
  return std::getenv(env_var);
}
std::pair<std::string, std::string>
get_host_port_from_full_addr(const char *full_url) {
  std::string full_str(full_url);
  size_t found_port = full_str.find_last_of(':');
  if (found_port == std::string::npos)
    throw std::runtime_error("Error : url has no port specified");
  std::string port = full_str.substr(found_port + 1);

  size_t found_host = full_str.find_last_of('/');
  if (found_host == std::string::npos)
    throw std::runtime_error("Error : url has no protocol specified");

  // Get the substr between protocol and port (ip)
  std::string host =
      full_str.substr(found_host + 1, found_port - (found_host + 1));

  return {host, port};
}

// returns host and port from env var HTTP_RECEPTOR_URL
std::pair<std::string, std::string> get_receptor_host_port() {
  const char *full_addr = get_url_from_env(K_RECEPTOR_ENV_ADDR);
  if (full_addr) {
    return get_host_port_from_full_addr(full_addr);
  } else {
    return std::pair("localhost", "8126");
  }
}

void fill_mock_exporter_input(ExporterInput &exporter_input,
                              std::pair<std::string, std::string> &url,
                              bool fill_valid_key) {
  if (fill_valid_key) {
    exporter_input.api_key = "yisthisisanapi_keyof32charslooong";
  } else {
    // agent for local tests (not taken as key)
    exporter_input.api_key = "nope_not_a_good_key";
  }

  exporter_input.agentless = "yes";
  exporter_input.environment = "unit-test";
  exporter_input.host = url.first.c_str();
  exporter_input.url = "datadog_is_cool.com";
  exporter_input.port = url.second.c_str();
  exporter_input.service = MYNAME;
  exporter_input.service_version = "42";
  exporter_input.do_export = "yes";
  exporter_input.debug_pprof_prefix = "some_prefix";
  exporter_input.user_agent = "DDPROF_MOCK";
  exporter_input.language = "NATIVE";
  exporter_input.family = "SANCHEZ";
  exporter_input.profiler_version = "1.1.2";
}

TEST(DDProfExporter, url) {
  LogHandle handle;
  std::pair<std::string, std::string> url{"25.04.1988.0", "1234"};
  ExporterInput exporter_input;
  // Test the site / host / port / API logic
  // If API key --> use site
  fill_mock_exporter_input(exporter_input, url, true);
  DDProfExporter exporter;
  DDRes res = ddprof_exporter_init(exporter_input, &exporter);
  EXPECT_TRUE(IsDDResOK(res));
  EXPECT_EQ(exporter._url, "datadog_is_cool.com");
  ddprof_exporter_free(&exporter);

  // To be discussed : should we fail here ?
  exporter_input.url = {};
  res = ddprof_exporter_init(exporter_input, &exporter);
  EXPECT_TRUE(IsDDResOK(res));
  EXPECT_EQ(exporter._url, "http://25.04.1988.0:1234");
  ddprof_exporter_free(&exporter);

  // If no API key --> expect host
  fill_mock_exporter_input(exporter_input, url, false);
  exporter_input.url = {};
  res = ddprof_exporter_init(exporter_input, &exporter);
  EXPECT_TRUE(IsDDResOK(res));
  EXPECT_EQ(exporter._url, "http://25.04.1988.0:1234");
  ddprof_exporter_free(&exporter);

  // UDS --> expect UDS
  fill_mock_exporter_input(exporter_input, url, false);
  exporter_input.url = "unix:///some/uds/socket.sock";
  res = ddprof_exporter_init(exporter_input, &exporter);
  EXPECT_TRUE(IsDDResOK(res));
  EXPECT_EQ(exporter._url, "unix:///some/uds/socket.sock");
  ddprof_exporter_free(&exporter);

  // UDS --> starts with a '/'
  fill_mock_exporter_input(exporter_input, url, false);
  exporter_input.url = "/some/uds/socket.sock";
  res = ddprof_exporter_init(exporter_input, &exporter);
  EXPECT_TRUE(IsDDResOK(res));
  EXPECT_EQ(exporter._url, "unix:///some/uds/socket.sock");
  ddprof_exporter_free(&exporter);
}

TEST(DDProfExporter, simple) {
  LogHandle handle;
  ExporterInput exporter_input;
  // Warning : Leave URL alive
  std::pair<std::string, std::string> url = get_receptor_host_port();

  { // setup input parameters
    fill_mock_exporter_input(exporter_input, url, false);
  }
  DDProfPProf pprofs;
  DDProfExporter exporter;
  DDRes res = ddprof_exporter_init(exporter_input, &exporter);
  EXPECT_TRUE(IsDDResOK(res));
  { // override folder to write debug pprofs
    // You can view content using : pprof -raw ./test/data/ddprof_
    exporter._debug_pprof_prefix = UNIT_TEST_DATA "/ddprof_";
  }

  { // Aggregate pprofs
    SymbolHdr symbol_hdr;
    UnwindOutput mock_output;
    FileInfoVector file_infos;
    SymbolTable &table = symbol_hdr._symbol_table;
    MapInfoTable &mapinfo_table = symbol_hdr._mapinfo_table;
    fill_unwind_symbols(table, mapinfo_table, mock_output);
    DDProfContext ctx = {};
    ctx.watchers.push_back(*ewatcher_from_str("sCPU"));
    res = pprof_create_profile(&pprofs, ctx);
    EXPECT_TRUE(IsDDResOK(res));
    res = pprof_aggregate(&mock_output, symbol_hdr, {1000, 1, 0},
                          &ctx.watchers[0], file_infos, false, kSumPos,
                          ctx.worker_ctx.symbolizer, &pprofs);
    EXPECT_TRUE(IsDDResOK(res));
  }
  {
    UserTags user_tags({}, 4);

    res = ddprof_exporter_new(&user_tags, &exporter);
    EXPECT_TRUE(IsDDResOK(res));

    if (get_url_from_env(K_RECEPTOR_ENV_ADDR)) {
      // receptor is defined
      Tags empty_tags;
      res = ddprof_exporter_export(&pprofs._profile, empty_tags, 0, 
                                   {.seconds = time(nullptr), .nanoseconds = 0}, 
                                   &exporter);
      // We should not be able to send profiles (usually 404)
      EXPECT_FALSE(IsDDResOK(res));
    }
  }
  res = ddprof_exporter_free(&exporter);
  EXPECT_TRUE(IsDDResOK(res));

  res = pprof_free_profile(&pprofs);
  EXPECT_TRUE(IsDDResOK(res));
}

} // namespace ddprof

// todo very long url
