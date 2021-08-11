#include "cap_display.h"

#include "ddres.h"
#include "logger.h"

#include <sys/capability.h>
#include <unistd.h>

struct CapFlag2Text {
  const char *_text;
  cap_flag_t _value;
};

static const struct CapFlag2Text s_cap_flag_text[] = {
    {"CAP_EFFECTIVE", CAP_EFFECTIVE},
    {"CAP_INHERITABLE", CAP_INHERITABLE},
    {"CAP_PERMITTED", CAP_PERMITTED},
};

#define SZ_CAP_2_TEXT 3

DDRes log_capabilities(bool verbose) {
  LG_NTC("LOG CAPABILITIES");
  pid_t pid = getpid();
  cap_t cap_struct = cap_get_pid(pid);
  ssize_t text_size = 0;
  char *pcap_text = cap_to_text(cap_struct, &text_size);
  LG_NTC("Capabilities : %s", pcap_text);

  if (verbose) {
    for (int i = 0; i < CAP_LAST_CAP; ++i) {
      char *lcap = cap_to_name(i);
      cap_flag_value_t value;
      for (int flag_idx = 0; flag_idx < SZ_CAP_2_TEXT; ++flag_idx) {
        DDRES_CHECK_INT(cap_get_flag(cap_struct, i,
                                     s_cap_flag_text[flag_idx]._value, &value),
                        DD_WHAT_CAPLIB, "Error retrieving capabilities.");
        LG_NTC("Cap=%s, flag=%s --> %s", lcap, s_cap_flag_text[flag_idx]._text,
               value == CAP_SET ? "ON" : "OFF");
      }
      cap_free(lcap);
    }
  }

  cap_free(pcap_text);
  cap_free(cap_struct);
  return ddres_init();
}
