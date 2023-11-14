// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "cap_display.hpp"

#include "ddres.hpp"
#include "logger.hpp"

#include <sys/capability.h>
#include <unistd.h>

namespace ddprof {

namespace {
struct CapFlag2Text {
  const char *_text;
  cap_flag_t _value;
};

const struct CapFlag2Text s_cap_flag_text[] = {
    {"CAP_EFFECTIVE", CAP_EFFECTIVE},
    {"CAP_INHERITABLE", CAP_INHERITABLE},
    {"CAP_PERMITTED", CAP_PERMITTED},
};

} // namespace

DDRes log_capabilities(bool verbose) {
  pid_t const pid = getpid();
  cap_t cap_struct = cap_get_pid(pid);
  ssize_t text_size = 0;
  char *pcap_text = cap_to_text(cap_struct, &text_size);
  LG_NFO("Capabilities %s", pcap_text);

  if (verbose) {
    for (int i = 0; i < CAP_LAST_CAP; ++i) {
      char *lcap = cap_to_name(i);
      cap_flag_value_t value;
      for (auto flag_idx : s_cap_flag_text) {
        DDRES_CHECK_INT(cap_get_flag(cap_struct, i, flag_idx._value, &value),
                        DD_WHAT_CAPLIB, "Error retrieving capabilities.");
        LG_NTC("Cap=%s, flag=%s --> %s", lcap, flag_idx._text,
               value == CAP_SET ? "ON" : "OFF");
      }
      cap_free(lcap);
    }
  }

  cap_free(pcap_text);
  cap_free(cap_struct);
  return {};
}

} // namespace ddprof
