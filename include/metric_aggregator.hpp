// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <algorithm>
#include <map>
#include <string>

#include "ddres.hpp"
#include "statsd.hpp"

struct MetricAggregator{
  std::string base_path = "profiler.native.";
  std::string sockpath = "/var/run/datadog-agent/statsd.sock";
  std::unordered_map<std::string, uint64_t> values;

  void add(const std::string &str, uint64_t val) {
    if (auto _f = values.find(str); _f == values.end())
      values[str] = 0;
    values[str] = values[str] + val;
  }

  void clear() {
    values.clear();
  }

  bool send() {
    PRINT_NFO("Preparing to send metrics");

    int fd = -1;
    if (IsDDResNotOK(statsd_connect(sockpath.c_str(), sockpath.size(), &fd)) ||
        -1 == fd) {
      LG_ERR("Could not connect to socket %s", sockpath.c_str());
      return false;
    }
    for (const auto &pair : values) {
      std::string metric_name = base_path + pair.first;
      void *coerced_val = (void *)&pair.second;
      if (IsDDResNotOK(statsd_send(fd, metric_name.c_str(), coerced_val, STAT_GAUGE))) {
        LG_ERR("Could not send metric %s on fd %d", metric_name.c_str(), fd);
      } else {
        PRINT_NFO("Sent metric %s of value %lu", metric_name.c_str(), pair.second);
      }
    }
    statsd_close(fd);

    // Since we sent the metrics, clear them
    clear();
    return true;
  }
};
