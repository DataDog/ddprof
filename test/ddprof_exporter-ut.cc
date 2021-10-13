#include "exporter/ddprof_exporter.h"

extern "C" {
#include "pprof/ddprof_pprof.h"
}

#include "loghandle.hpp"
#include "tags.hpp"
#include "unwind_output_mock.hpp"

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <utility>

#include "unwind_symbols.hpp"

namespace ddprof {
// todo : cut this dependency
DwflSymbolLookup_V2::DwflSymbolLookup_V2() : _lookup_setting(K_CACHE_ON) {}

// Mock
int get_nb_hw_thread() { return 2; }

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

  return std::make_pair<std::string, std::string>(std::move(host),
                                                  std::move(port));
}

// returns host and port from env var HTTP_RECEPTOR_URL
std::pair<std::string, std::string> get_receptor_host_port() {
  const char *full_addr = get_url_from_env(K_RECEPTOR_ENV_ADDR);
  if (full_addr) {
    return get_host_port_from_full_addr(full_addr);
  } else {
    return std::make_pair<std::string, std::string>("localhost", "8126");
  }
}

void fill_mock_exporter_input(ExporterInput *exporter_input,
                              std::pair<std::string, std::string> &url) {
  exporter_input->apikey = "abc"; // agent for local tests (not taken as key)
                                  // "yisthisisanapikeyof32charslooong";

  exporter_input->environment = "unit-test";
  exporter_input->host = url.first.c_str();
  exporter_input->site = "whatever is a site";
  exporter_input->port = url.second.c_str();
  exporter_input->service = MYNAME;
  exporter_input->serviceversion = "42";
  exporter_input->user_agent = STRING_VIEW_LITERAL("DDPROF_MOCK");
  exporter_input->language = STRING_VIEW_LITERAL("NATIVE");
  exporter_input->family = STRING_VIEW_LITERAL("SANCHEZ");
  exporter_input->profiler_version = STRING_VIEW_LITERAL("1.1.2");
}

TEST(DDProfExporter, simple) {
  LogHandle handle;
  ExporterInput exporter_input;
  // Warning : Leave URL alive
  std::pair<std::string, std::string> url = ddprof::get_receptor_host_port();

  { // setup input parameters
    fill_mock_exporter_input(&exporter_input, url);
  }
  DDProfPProf pprofs;
  DDProfExporter exporter;
  DDRes res = ddprof_exporter_init(&exporter_input, &exporter);
  EXPECT_TRUE(IsDDResOK(res));
  { // override folder to write debug pprofs
    // You can view content using : pprof -raw ./test/data/ddprof_
    exporter._debug_folder = IPC_TEST_DATA;
  }

  { // Aggregate pprofs
    UnwindSymbolsHdr symbols_hdr;
    UnwindOutput mock_output;
    SymbolTable &table = symbols_hdr._symbol_table;
    MapInfoTable &mapinfo_table = symbols_hdr._mapinfo_table;

    fill_unwind_symbols(table, mapinfo_table, mock_output);

    const PerfOption *perf_option_cpu = perfoptions_preset(10);
    res = pprof_create_profile(&pprofs, perf_option_cpu, 1);
    EXPECT_TRUE(IsDDResOK(res));
    res = pprof_aggregate(&mock_output, &symbols_hdr, 1000, 0, &pprofs);
    EXPECT_TRUE(IsDDResOK(res));
  }
  {
    UserTags user_tags(nullptr, 4);

    res = ddprof_exporter_new(&user_tags, &exporter);
    EXPECT_TRUE(IsDDResOK(res));

    res = ddprof_exporter_export(pprofs._profile, &exporter);
    // if url is set, expect success
    if (ddprof::get_url_from_env(K_RECEPTOR_ENV_ADDR)) {
      EXPECT_TRUE(IsDDResOK(res));
    }
  }
  res = ddprof_exporter_free(&exporter);
  EXPECT_TRUE(IsDDResOK(res));

  res = pprof_free_profile(&pprofs);
  EXPECT_TRUE(IsDDResOK(res));
}

} // namespace ddprof

// todo very long url
